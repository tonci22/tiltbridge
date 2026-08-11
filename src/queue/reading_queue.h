#ifndef TILTBRIDGE_READING_QUEUE_H
#define TILTBRIDGE_READING_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <ArduinoJson.h>

#include "reading_record.h"
#include "../filesystem.h"

#define QUEUE_DIR          FILESYSTEM_PREFIX "/queue"
#define QUEUE_JOURNAL_PATH QUEUE_DIR "/journal.bin"
#define QUEUE_STATE_PATH   QUEUE_DIR "/state.bin"

// Refuse to append when free filesystem space drops below this, so the queue can never
// starve config saves or make the device unflashable.
#define QUEUE_MIN_FREE_BYTES 32768

// Upper bound on maxQueuedRecords regardless of free space, matching the config validator.
#define QUEUE_MAX_RECORDS_CEILING 3000

// Terminal outcomes recorded in the journal.
#define QUEUE_JOURNAL_ACK  1
#define QUEUE_JOURNAL_DROP 2

// Out-of-order terminations held in RAM above the contiguous head. One in-flight batch is
// the realistic worst case; the cap simply bounds memory if a server misbehaves.
#define QUEUE_MAX_SPARSE_TERMINATED 64

#pragma pack(push, 1)
struct QueueJournalEntry {
    uint32_t sequence;
    uint8_t  kind;
    uint8_t  pad[3];
};
#pragma pack(pop)

static_assert(sizeof(QueueJournalEntry) == 8, "journal entry must be 8 bytes");

/**
 * @brief Append-only persistent reading queue.
 *
 * Layout: numbered 8 KB segment files of fixed 128-byte records, plus an append-only
 * journal of terminal outcomes. Nothing on flash is ever rewritten in place, so an
 * interrupted write can only damage the trailing entry of one file, which is discarded on
 * load. Records are consumed strictly FIFO.
 */
class ReadingQueue {
public:
    bool init();

    // Producer. fsyncs before returning true, so a committed record survives power loss.
    bool append(QueuedReading &rec);

    // Allocate the next id components for a device. Call immediately before filling a record.
    void assignIdentity(QueuedReading &rec, const char *deviceId);

    // Consumer. Fills up to maxRecords un-terminated records in FIFO order.
    size_t peekBatch(QueuedReading *out, size_t maxRecords);

    // Terminal outcomes. acknowledge() takes a formatted record id as returned by the server.
    bool acknowledgeId(const char *recordId);
    void compact();
    bool clear();

    /**
     * @brief Delete every trace of the queue, including the state file and the NVS counters.
     *
     * clear() drops the queued readings but deliberately keeps the boot counter and the
     * overflow tally, which is right for "empty the queue" from the UI. A factory reset must
     * leave nothing behind, so it uses this instead.
     */
    void eraseAll();

    // Status
    size_t   pendingCount() const { return m_pendingCount; }
    uint32_t droppedOverflow() const { return m_droppedOverflow; }
    uint32_t oldestPendingAgeSec();
    size_t   bytesUsed() const { return m_pendingCount * QUEUE_RECORD_SIZE; }
    uint8_t  storagePercent() const;
    bool     isHealthy() const { return m_healthy; }

    // Records this filesystem can actually hold, or 0 if it cannot be queried. The
    // configured maxQueuedRecords is capped by this at append time.
    uint16_t storageCapacityRecords() const;
    void     to_json(JsonDocument &doc);

    uint32_t bootId() const { return m_bootId; }

private:
    bool  ensureDir();
    bool  appendJournal(uint32_t sequence, uint8_t kind);
    bool  isTerminated(uint32_t sequence) const;
    void  markTerminated(uint32_t sequence);
    void  advanceHead();
    bool  dropOldest();
    void  segmentPath(uint32_t segment, char *out, size_t outSize) const;
    void  scanSegments();
    void  loadJournal();
    void  rewriteJournal();
    bool  recomputePending();

    uint32_t m_bootId = 0;

    /*
     * Truncated bootIds of the records actually on flash. Records outlive the session that
     * wrote them - that is the point of the queue - so an acknowledgement can legitimately
     * name a bootId that is not the current one, and it must still retire the record.
     * Realistically this holds one or two entries; the cap only bounds the memory.
     */
    static const uint8_t MAX_RETAINED_BOOT_IDS = 4;
    uint32_t m_retainedBootIds[MAX_RETAINED_BOOT_IDS] = {};
    uint8_t  m_retainedBootIdCount = 0;

    void noteRetainedBootId(uint32_t bootId);
    bool isRetainedBootId(uint32_t bootId) const;
    uint32_t m_nextSequence = 1;
    uint32_t m_headSequence = 0;        // every sequence <= this is terminated
    uint32_t m_droppedOverflow = 0;

    uint32_t m_firstSegment = 0;
    uint32_t m_lastSegment = 0;
    uint16_t m_lastSegmentCount = 0;    // records in the newest segment
    size_t   m_pendingCount = 0;
    bool     m_healthy = false;

    uint32_t m_sparseTerminated[QUEUE_MAX_SPARSE_TERMINATED] = {};
    size_t   m_sparseCount = 0;
};

extern ReadingQueue reading_queue;

#endif // TILTBRIDGE_READING_QUEUE_H
