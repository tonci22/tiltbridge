#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_littlefs.h>
#include <esp_mac.h>
#include <nvs.h>
#include <thorlog.h>

#include <esp_random.h>
#include "reading_queue.h"
#include "../jsonconfig.h"
#include "../sender_health.h"   // sh_millis()
#include "../time_sync.h"

ReadingQueue reading_queue;

#define NVS_QUEUE_NAMESPACE "tbqueue"
#define NVS_BOOT_COUNTER_KEY "bootc"
#define NVS_DROPPED_KEY      "dropped"

//=============================================================================
// Helpers
//=============================================================================

void ReadingQueue::segmentPath(uint32_t segment, char *out, size_t outSize) const {
    snprintf(out, outSize, QUEUE_DIR "/seg-%06lu.bin", (unsigned long)segment);
}

bool ReadingQueue::ensureDir() {
    struct stat st;
    if (stat(QUEUE_DIR, &st) == 0)
        return true;

    if (mkdir(QUEUE_DIR, 0777) != 0) {
        Log.error("Unable to create queue directory %s.\r\n", QUEUE_DIR);
        return false;
    }
    return true;
}

static bool queue_fs_has_room() {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(FILESYSTEM_PARTITION, &total, &used) != ESP_OK)
        return true;    // can't tell; don't block writes on a failed query

    return (total - used) > QUEUE_MIN_FREE_BYTES;
}

//=============================================================================
// Terminal-outcome tracking
//=============================================================================

bool ReadingQueue::isTerminated(uint32_t sequence) const {
    if (sequence <= m_headSequence)
        return true;

    for (size_t i = 0; i < m_sparseCount; i++) {
        if (m_sparseTerminated[i] == sequence)
            return true;
    }
    return false;
}

void ReadingQueue::markTerminated(uint32_t sequence) {
    if (isTerminated(sequence))
        return;

    if (m_sparseCount < QUEUE_MAX_SPARSE_TERMINATED) {
        m_sparseTerminated[m_sparseCount++] = sequence;
    } else {
        // Should not happen with a bounded batch size. Losing the marker only means the
        // record is offered again and the server dedups it - at-least-once still holds.
        Log.warning("Queue: sparse termination table full; sequence %lu may be resent.\r\n",
                    (unsigned long)sequence);
        return;
    }

    advanceHead();
}

void ReadingQueue::advanceHead() {
    // Absorb every contiguous terminated sequence into the head, compacting the sparse set.
    bool moved;
    do {
        moved = false;
        for (size_t i = 0; i < m_sparseCount; i++) {
            if (m_sparseTerminated[i] == m_headSequence + 1) {
                m_headSequence++;
                m_sparseTerminated[i] = m_sparseTerminated[m_sparseCount - 1];
                m_sparseCount--;
                moved = true;
                break;
            }
        }
    } while (moved);
}

bool ReadingQueue::appendJournal(uint32_t sequence, uint8_t kind) {
    FILE *f = fopen(QUEUE_JOURNAL_PATH, "ab");
    if (f == nullptr) {
        Log.error("Queue: unable to open journal for append.\r\n");
        return false;
    }

    QueueJournalEntry e{};
    e.sequence = sequence;
    e.kind = kind;

    const size_t written = fwrite(&e, 1, sizeof(e), f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return written == sizeof(e);
}

//=============================================================================
// Init / recovery
//=============================================================================

bool ReadingQueue::init() {
    m_healthy = false;

    if (!ensureDir())
        return false;

    // Boot id: unique per boot session and per device. One NVS write per boot.
    uint32_t bootCounter = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_QUEUE_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_BOOT_COUNTER_KEY, &bootCounter);
        bootCounter++;
        nvs_set_u32(h, NVS_BOOT_COUNTER_KEY, bootCounter);
        nvs_get_u32(h, NVS_DROPPED_KEY, &m_droppedOverflow);
        nvs_commit(h);
        nvs_close(h);
    }

    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);

    /*
     * Three ingredients, because a record id that repeats is silently destructive: the server
     * suppresses it as a duplicate, acknowledges it, and the reading is gone without an error
     * anywhere.
     *
     *   MAC          distinguishes devices sharing a spreadsheet
     *   bootCounter  monotonic across the device's life, so boots never collide
     *   esp_random   makes a replay impossible even if NVS is unavailable or was erased,
     *                which is exactly what a counter alone cannot survive
     *
     * The sequence number restarts at 1 whenever the queue is empty, which is now the normal
     * state, so uniqueness rests entirely on this value differing every boot.
     */
    m_bootId = qr_crc32(mac, sizeof(mac))
             ^ (bootCounter * 0x9E3779B9u)
             ^ esp_random();

    scanSegments();
    loadJournal();
    advanceHead();
    recomputePending();
    compact();

    m_healthy = true;
    Log.notice("Queue ready: %u pending, segments %lu..%lu, next sequence %lu, bootId %06lX.\r\n",
               (unsigned)m_pendingCount,
               (unsigned long)m_firstSegment, (unsigned long)m_lastSegment,
               (unsigned long)m_nextSequence, (unsigned long)(m_bootId & 0xFFFFFF));
    return true;
}

void ReadingQueue::scanSegments() {
    m_firstSegment = 0;
    m_lastSegment = 0;
    m_lastSegmentCount = 0;
    m_nextSequence = 1;

    DIR *dir = opendir(QUEUE_DIR);
    if (dir == nullptr)
        return;

    uint32_t lo = UINT32_MAX, hi = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        unsigned long n = 0;
        if (sscanf(entry->d_name, "seg-%06lu.bin", &n) != 1)
            continue;
        if ((uint32_t)n < lo) lo = (uint32_t)n;
        if ((uint32_t)n > hi) hi = (uint32_t)n;
    }
    closedir(dir);

    if (lo == UINT32_MAX)
        return;     // no segments yet

    m_firstSegment = lo;
    m_lastSegment = hi;

    // Walk every segment to find the highest sequence and to trim a torn tail. Reading
    // ~256 KB sequentially costs a fraction of a second and removes the need for a
    // separate durable "next sequence" counter.
    uint32_t maxSequence = 0;

    for (uint32_t seg = lo; seg <= hi; seg++) {
        char path[64];
        segmentPath(seg, path, sizeof(path));

        FILE *f = fopen(path, "rb");
        if (f == nullptr)
            continue;

        uint16_t valid = 0;
        QueuedReading rec;
        for (;;) {
            const size_t got = fread(&rec, 1, QUEUE_RECORD_SIZE, f);
            if (got == 0)
                break;

            if (got < QUEUE_RECORD_SIZE) {
                Log.warning("Queue: segment %lu has a %u-byte partial trailing record; discarding it.\r\n",
                            (unsigned long)seg, (unsigned)got);
                break;
            }

            if (!qr_is_valid(rec)) {
                // Only the newest segment can legitimately have a torn tail. Anywhere
                // else this means real damage, so stop at the first bad record and keep
                // everything before it.
                Log.warning("Queue: segment %lu record %u failed validation; truncating there.\r\n",
                            (unsigned long)seg, (unsigned)valid);
                break;
            }

            if (rec.sequence > maxSequence)
                maxSequence = rec.sequence;
            valid++;
        }
        fclose(f);

        // Realign the file so future appends stay on a record boundary.
        const long wanted = (long)valid * QUEUE_RECORD_SIZE;
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size != wanted) {
            if (truncate(path, wanted) != 0)
                Log.error("Queue: unable to truncate segment %lu to %ld bytes.\r\n",
                          (unsigned long)seg, wanted);
        }

        if (seg == hi)
            m_lastSegmentCount = valid;
    }

    m_nextSequence = maxSequence + 1;
}

void ReadingQueue::loadJournal() {
    m_headSequence = 0;
    m_sparseCount = 0;

    FILE *f = fopen(QUEUE_JOURNAL_PATH, "rb");
    if (f == nullptr)
        return;

    QueueJournalEntry e;
    size_t entries = 0;
    for (;;) {
        const size_t got = fread(&e, 1, sizeof(e), f);
        if (got == 0)
            break;
        if (got < sizeof(e)) {
            // Torn final append; ignore it.
            break;
        }
        if (e.kind != QUEUE_JOURNAL_ACK && e.kind != QUEUE_JOURNAL_DROP)
            continue;

        markTerminated(e.sequence);
        entries++;
    }
    fclose(f);

    if (entries > 0)
        Log.info("Queue: replayed %u journal entries, head at sequence %lu.\r\n",
                 (unsigned)entries, (unsigned long)m_headSequence);
}

bool ReadingQueue::recomputePending() {
    m_pendingCount = 0;

    if (m_firstSegment == 0 && m_lastSegment == 0)
        return true;

    for (uint32_t seg = m_firstSegment; seg <= m_lastSegment; seg++) {
        char path[64];
        segmentPath(seg, path, sizeof(path));

        FILE *f = fopen(path, "rb");
        if (f == nullptr)
            continue;

        QueuedReading rec;
        while (fread(&rec, 1, QUEUE_RECORD_SIZE, f) == QUEUE_RECORD_SIZE) {
            if (qr_is_valid(rec) && !isTerminated(rec.sequence))
                m_pendingCount++;
        }
        fclose(f);
    }
    return true;
}

/**
 * @brief How many records this filesystem can actually hold for the queue.
 *
 * maxQueuedRecords is configurable up to 3000, which is 384 KB - more than the LittleFS
 * partition has spare once the web UI is on it. Reporting the real ceiling lets the UI stop
 * offering values the flash cannot honour, and lets append() shed rather than wedge.
 *
 * Space the queue already occupies counts as available: it comes back as the queue drains,
 * so the answer stays stable instead of shrinking as the backlog grows.
 *
 * @return records, or 0 when the filesystem cannot be queried.
 */
uint16_t ReadingQueue::storageCapacityRecords() const {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(FILESYSTEM_PARTITION, &total, &used) != ESP_OK)
        return 0;

    const size_t freeNow = (total > used) ? (total - used) : 0;
    const size_t ownBytes = (size_t)m_pendingCount * QUEUE_RECORD_SIZE;
    const size_t claimable = freeNow + ownBytes;

    if (claimable <= QUEUE_MIN_FREE_BYTES)
        return 0;

    // LittleFS allocates in blocks and the journal grows alongside the segments, so promise
    // four fifths of the arithmetic maximum rather than every last byte.
    size_t records = ((claimable - QUEUE_MIN_FREE_BYTES) / QUEUE_RECORD_SIZE) * 4 / 5;

    if (records > QUEUE_MAX_RECORDS_CEILING)
        records = QUEUE_MAX_RECORDS_CEILING;

    return (uint16_t)records;
}

//=============================================================================
// Append
//=============================================================================

void ReadingQueue::assignIdentity(QueuedReading &rec, const char *deviceId) {
    rec.bootId = m_bootId;
    rec.deviceHash = qr_crc16(deviceId);
    rec.sequence = m_nextSequence++;
}

bool ReadingQueue::append(QueuedReading &rec) {
    if (!m_healthy)
        return false;

    /*
     * Space, not the record cap, is usually the binding limit on a filesystem shared with the
     * web UI - maxQueuedRecords can be set higher than the flash can actually hold. Treat it
     * exactly like the record cap does: shed the OLDEST records until there is room, rather
     * than refusing and throwing away the newest reading while the queue stays wedged.
     *
     * Shedding is bounded. A segment is only reclaimed once every record in it is terminated,
     * so freeing space takes a segment's worth of drops; if two segments' worth does not help,
     * the space is being consumed by something other than the queue and dropping more would
     * destroy the backlog for nothing.
     */
    if (!queue_fs_has_room()) {
        uint16_t shed = 0;
        const uint16_t shedLimit = QUEUE_RECORDS_PER_SEGMENT * 2;

        while (!queue_fs_has_room() && shed < shedLimit) {
            if (!dropOldest())
                break;
            compact();
            shed++;
        }

        if (!queue_fs_has_room()) {
            Log.error("QUEUE_FS_FULL: refusing to append, filesystem free space below %u bytes "
                      "after shedding %u record%s.\r\n",
                      (unsigned)QUEUE_MIN_FREE_BYTES, (unsigned)shed, (shed == 1) ? "" : "s");
            return false;
        }

        Log.warning("Queue: shed %u oldest record%s to stay within the filesystem reserve. "
                    "maxQueuedRecords (%u) is above what this flash can hold.\r\n",
                    (unsigned)shed, (shed == 1) ? "" : "s", (unsigned)config.maxQueuedRecords);
    }

    // Enforce the record cap before adding, dropping the oldest un-terminated record. The
    // effective cap is the lower of what was configured and what the flash can hold, so a
    // setting the storage cannot honour degrades to dropping rather than to failing.
    uint16_t cap = config.maxQueuedRecords;
    const uint16_t storageCap = storageCapacityRecords();
    if (storageCap > 0 && storageCap < cap)
        cap = storageCap;

    while (m_pendingCount >= cap) {
        if (!dropOldest())
            break;
    }

    if (m_lastSegment == 0) {
        m_firstSegment = 1;
        m_lastSegment = 1;
        m_lastSegmentCount = 0;
    } else if (m_lastSegmentCount >= QUEUE_RECORDS_PER_SEGMENT) {
        m_lastSegment++;
        m_lastSegmentCount = 0;
    }

    char path[64];
    segmentPath(m_lastSegment, path, sizeof(path));

    FILE *f = fopen(path, "ab");
    if (f == nullptr) {
        Log.error("Queue: unable to open segment %lu for append.\r\n", (unsigned long)m_lastSegment);
        return false;
    }

    qr_finalize(rec);
    const size_t written = fwrite(&rec, 1, QUEUE_RECORD_SIZE, f);

    // Without the fsync the record can live only in the LittleFS cache and be lost to a
    // power cut, which is precisely what this queue exists to survive.
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (written != QUEUE_RECORD_SIZE) {
        Log.error("Queue: short write (%u bytes) appending record.\r\n", (unsigned)written);
        return false;
    }

    m_lastSegmentCount++;
    m_pendingCount++;

    char id[QR_RECORD_ID_LEN];
    qr_format_record_id(rec, id, sizeof(id));
    Log.info("Queue: stored %s for %s (%u pending).\r\n", id, rec.deviceId, (unsigned)m_pendingCount);
    return true;
}

bool ReadingQueue::dropOldest() {
    QueuedReading oldest;
    bool found = false;

    for (uint32_t seg = m_firstSegment; seg <= m_lastSegment && !found; seg++) {
        char path[64];
        segmentPath(seg, path, sizeof(path));

        FILE *f = fopen(path, "rb");
        if (f == nullptr)
            continue;

        QueuedReading rec;
        while (fread(&rec, 1, QUEUE_RECORD_SIZE, f) == QUEUE_RECORD_SIZE) {
            if (qr_is_valid(rec) && !isTerminated(rec.sequence)) {
                oldest = rec;
                found = true;
                break;
            }
        }
        fclose(f);
    }

    if (!found)
        return false;

    appendJournal(oldest.sequence, QUEUE_JOURNAL_DROP);
    markTerminated(oldest.sequence);
    if (m_pendingCount > 0)
        m_pendingCount--;

    m_droppedOverflow++;

    // Persist the counter so an overflow stays visible across a reboot. Rare by
    // definition, so the NVS write costs nothing in practice.
    nvs_handle_t h;
    if (nvs_open(NVS_QUEUE_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_DROPPED_KEY, m_droppedOverflow);
        nvs_commit(h);
        nvs_close(h);
    }

    char id[QR_RECORD_ID_LEN];
    qr_format_record_id(oldest, id, sizeof(id));
    Log.warning("QUEUE_OVERFLOW: discarded oldest record %s (cap %u, dropped total %lu).\r\n",
                id, (unsigned)config.maxQueuedRecords, (unsigned long)m_droppedOverflow);
    return true;
}

//=============================================================================
// Consume
//=============================================================================

size_t ReadingQueue::peekBatch(QueuedReading *out, size_t maxRecords) {
    if (out == nullptr || maxRecords == 0 || !m_healthy)
        return 0;

    size_t n = 0;

    for (uint32_t seg = m_firstSegment; seg <= m_lastSegment && n < maxRecords; seg++) {
        char path[64];
        segmentPath(seg, path, sizeof(path));

        FILE *f = fopen(path, "rb");
        if (f == nullptr)
            continue;

        QueuedReading rec;
        while (n < maxRecords && fread(&rec, 1, QUEUE_RECORD_SIZE, f) == QUEUE_RECORD_SIZE) {
            if (qr_is_valid(rec) && !isTerminated(rec.sequence))
                out[n++] = rec;
        }
        fclose(f);
    }

    return n;
}

bool ReadingQueue::acknowledgeId(const char *recordId) {
    if (recordId == nullptr || !m_healthy)
        return false;

    // Parse "<bootId>-<deviceHash>-<sequence>" back to its components. Only the sequence
    // is needed to terminate a record, but the bootId is checked so an id from a previous
    // session cannot retire a current record.
    unsigned long bootId = 0, sequence = 0;
    unsigned int deviceHash = 0;
    if (sscanf(recordId, "%06lX-%04X-%08lX", &bootId, &deviceHash, &sequence) != 3)
        return false;

    if ((uint32_t)bootId != (m_bootId & 0x00FFFFFFu)) {
        // From an earlier boot: the record is already gone, so treat it as a no-op rather
        // than an error. The server is allowed to re-acknowledge ids it has seen before.
        return true;
    }

    if (isTerminated((uint32_t)sequence))
        return true;

    if (!appendJournal((uint32_t)sequence, QUEUE_JOURNAL_ACK))
        return false;

    markTerminated((uint32_t)sequence);
    if (m_pendingCount > 0)
        m_pendingCount--;

    return true;
}

//=============================================================================
// Maintenance
//=============================================================================

void ReadingQueue::compact() {
    if (m_firstSegment == 0)
        return;

    // Delete leading segments in which every record is terminated. Never touch the
    // segment currently being appended to.
    while (m_firstSegment < m_lastSegment) {
        char path[64];
        segmentPath(m_firstSegment, path, sizeof(path));

        FILE *f = fopen(path, "rb");
        if (f == nullptr) {
            m_firstSegment++;
            continue;
        }

        bool allTerminated = true;
        QueuedReading rec;
        while (fread(&rec, 1, QUEUE_RECORD_SIZE, f) == QUEUE_RECORD_SIZE) {
            if (qr_is_valid(rec) && !isTerminated(rec.sequence)) {
                allTerminated = false;
                break;
            }
        }
        fclose(f);

        if (!allTerminated)
            break;

        if (remove(path) != 0)
            break;

        Log.info("Queue: removed fully-delivered segment %lu.\r\n", (unsigned long)m_firstSegment);
        m_firstSegment++;
    }

    rewriteJournal();
}

void ReadingQueue::rewriteJournal() {
    // Entries at or below the head are implied by m_headSequence, so the journal only
    // needs to carry the sparse remainder. This keeps it from growing without bound while
    // still being a pure rewrite of a small file (never of the records themselves).
    struct stat st;
    if (stat(QUEUE_JOURNAL_PATH, &st) != 0)
        return;

    // Only worth doing once the file has grown past a few hundred entries.
    if (st.st_size < 2048)
        return;

    FILE *f = fopen(QUEUE_JOURNAL_PATH ".tmp", "wb");
    if (f == nullptr)
        return;

    // One entry establishes the head; the rest carry out-of-order terminations.
    if (m_headSequence > 0) {
        QueueJournalEntry e{};
        e.sequence = m_headSequence;
        e.kind = QUEUE_JOURNAL_ACK;
        fwrite(&e, 1, sizeof(e), f);
    }
    for (size_t i = 0; i < m_sparseCount; i++) {
        QueueJournalEntry e{};
        e.sequence = m_sparseTerminated[i];
        e.kind = QUEUE_JOURNAL_ACK;
        fwrite(&e, 1, sizeof(e), f);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    // A head-only journal loses the "every sequence below the head is done" fact, so
    // re-establish it on load by treating the single head entry as authoritative. That is
    // what markTerminated() + advanceHead() do when they replay it.
    if (rename(QUEUE_JOURNAL_PATH ".tmp", QUEUE_JOURNAL_PATH) != 0)
        remove(QUEUE_JOURNAL_PATH ".tmp");
}

void ReadingQueue::eraseAll() {
    clear();                        // segments and the journal

    ::remove(QUEUE_STATE_PATH);     // clear() keeps this; a factory reset must not

    /*
     * Clear the overflow tally, but NEVER the boot counter.
     *
     * The counter is what keeps record ids unique across the device's whole life. Erasing it
     * restarts bootId at a value already used, and since the sequence also restarts at 1 the
     * device then re-emits ids the server has already seen - which it suppresses as
     * duplicates, so the readings vanish silently. A factory reset must not be able to cause
     * that, so the counter survives it deliberately.
     */
    nvs_handle_t h;
    if (nvs_open(NVS_QUEUE_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_DROPPED_KEY);
        nvs_commit(h);
        nvs_close(h);
    }

    m_droppedOverflow = 0;
    m_nextSequence = 1;
    m_headSequence = 0;

    Log.warning("Queue erased for factory reset.\r\n");
}

bool ReadingQueue::clear() {
    DIR *dir = opendir(QUEUE_DIR);
    if (dir != nullptr) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            unsigned long n = 0;
            if (sscanf(entry->d_name, "seg-%06lu.bin", &n) != 1)
                continue;
            char path[64];
            segmentPath((uint32_t)n, path, sizeof(path));
            remove(path);
        }
        closedir(dir);
    }

    remove(QUEUE_JOURNAL_PATH);

    m_firstSegment = 0;
    m_lastSegment = 0;
    m_lastSegmentCount = 0;
    m_pendingCount = 0;
    m_headSequence = m_nextSequence > 0 ? m_nextSequence - 1 : 0;
    m_sparseCount = 0;

    Log.warning("Queue cleared by request.\r\n");
    return true;
}

//=============================================================================
// Status
//=============================================================================

uint32_t ReadingQueue::oldestPendingAgeSec() {
    QueuedReading rec;
    if (peekBatch(&rec, 1) == 0)
        return 0;

    // Prefer real elapsed time; fall back to uptime-relative when the clock was not set
    // at capture (or is not set now).
    if ((rec.flags & QR_FLAG_TIMESTAMP_VALID) && rec.capturedAtUtc != 0) {
        const uint32_t now = utc_now();
        if (now > rec.capturedAtUtc)
            return now - rec.capturedAtUtc;
    }

    const uint32_t nowMs = sh_millis();
    if (nowMs > rec.capturedAtUptimeMs)
        return (nowMs - rec.capturedAtUptimeMs) / 1000;

    return 0;
}

uint8_t ReadingQueue::storagePercent() const {
    const uint32_t cap = config.maxQueuedRecords;
    if (cap == 0)
        return 0;

    uint32_t pct = (uint32_t)((m_pendingCount * 100ULL) / cap);
    return (uint8_t)(pct > 100 ? 100 : pct);
}

void ReadingQueue::to_json(JsonDocument &doc) {
    doc["queuedReadings"] = (uint32_t)m_pendingCount;
    doc["oldestReadingAgeSec"] = oldestPendingAgeSec();
    doc["storagePercent"] = storagePercent();
    doc["bytesUsed"] = (uint32_t)bytesUsed();
    doc["maxRecords"] = config.maxQueuedRecords;
    doc["snapshotIntervalSec"] = config.queueSnapshotIntervalSec;
    doc["droppedOverflow"] = m_droppedOverflow;
    doc["timeValid"] = time_is_valid();
    doc["recordSize"] = QUEUE_RECORD_SIZE;
    doc["healthy"] = m_healthy;
    doc["enabled"] = config.offlineQueueEnabled;
    doc["batchSize"] = config.queueBatchSize;

    size_t total = 0, used = 0;
    if (esp_littlefs_info(FILESYSTEM_PARTITION, &total, &used) == ESP_OK)
        doc["fsFreeBytes"] = (uint32_t)(total - used);
    else
        doc["fsFreeBytes"] = nullptr;
}
