# Stage 8: Persistent offline reading queue

Spec §7, §9, §10, §11, §12, §25, §26.

## Budget

LittleFS partition: `0xD0000` = 851,968 B. Currently used: UI ≈ 24 KB + config ≈ 4 KB +
calibration files. Call it ~800 KB free.

Fixed record size **128 B** → default cap 1500 records = **192 KB**; 2000 records = 256 KB. Both
fit comfortably with room for the UI to grow. Hard ceiling enforced in config validation:
`maxQueuedRecords` clamped to `[100, 3000]` (3000 = 384 KB).

## New files

```
src/queue/reading_record.h      # on-flash layout, CRC, record-id formatting
src/queue/reading_queue.h
src/queue/reading_queue.cpp
src/time_sync.h / .cpp          # minimal SNTP (§12) — no time source exists in the tree today
```

## On-flash layout

```
/littlefs/queue/seg-000001.bin   append-only, 64 records × 128 B = 8192 B per segment
/littlefs/queue/seg-000002.bin
/littlefs/queue/journal.bin      append-only, 8 B per entry: {u32 sequence, u8 kind, u8 pad[3]}
/littlefs/queue/state.bin        24 B, written at most once per snapshot interval
```

Segment numbers are monotonic, zero-padded to 6 digits so `readdir` order is usable.
`journal.bin` records terminal outcomes: `kind = 1` acknowledged, `kind = 2` dropped by overflow.

### Why a journal instead of in-place status bytes

Everything on flash is append-only (§26: "prefer an append/ring format where an interrupted final
write cannot destroy previously valid records"). A torn append damages only the trailing entry,
which is discarded on load. No record is ever rewritten, so a power cut can never corrupt data
that was already committed. Partial acknowledgement (§15) is handled purely by appending 8 bytes
per acked id.

### `struct QueuedReading` — exactly 128 bytes, `#pragma pack(1)`

```cpp
#define QUEUE_RECORD_MAGIC   0x51524231u   // 'QRB1'
#define QUEUE_RECORD_SIZE    128
#define QUEUE_RECORDS_PER_SEGMENT 64

#pragma pack(push, 1)
struct QueuedReading {
    uint32_t magic;              // QUEUE_RECORD_MAGIC
    uint8_t  version;            // 1
    uint8_t  flags;              // bit0 timestampValid, bit1 tiltPro
    uint16_t reserved0;

    // --- identity, §11 ---
    uint32_t bootId;             // unique per boot session
    uint16_t deviceHash;         // CRC16 of the canonical device id
    uint32_t sequence;           // monotonic within the boot session

    // --- device, §2 ---
    char     deviceId[18];       // "88:C2:55:AC:26:81", canonical uppercase
    uint8_t  colorIndex;         // 0..7
    char     sheetName[26];      // resolved googleSheetsName, snapshotted so the record is self-contained

    // --- readings, §6 ---
    float    tempF;              // always Fahrenheit, matching every existing payload
    float    gravity;            // normal final TiltBridge value (cal_smooth_gravity)
    float    gravityRaw;         // latest_gravity
    float    gravitySmoothed;    // uncal_smooth_gravity

    // --- RSSI, §4 ---
    int8_t   rssiLatest;
    int8_t   rssiAverage;
    int8_t   rssiMinimum;
    int8_t   rssiMaximum;
    uint16_t rssiSamples;

    // --- time, §12 ---
    uint32_t capturedAtUtc;      // unix seconds, 0 when never synced
    uint32_t capturedAtUptimeMs; // always valid; preserves ordering when capturedAtUtc is unusable

    uint8_t  padding[/* to 124 */];
    uint32_t crc32;              // CRC32 over the first (QUEUE_RECORD_SIZE - 4) bytes
};
#pragma pack(pop)
static_assert(sizeof(QueuedReading) == QUEUE_RECORD_SIZE, "record must be exactly 128 bytes");
```

Compute `padding` size from the running total when writing the header; let the `static_assert`
police it. Zero the whole struct before filling so padding is deterministic and the CRC is stable.

### Record ID (§11)

```cpp
// "A91F2C-6A72-00000452"
void formatRecordId(const QueuedReading &r, char *out, size_t outSize) {
    snprintf(out, outSize, "%06lX-%04X-%08lX",
             (unsigned long)(r.bootId & 0xFFFFFFu), r.deviceHash, (unsigned long)r.sequence);
}
```

- `bootId`: `bootCounter` from NVS (namespace `tbqueue`, key `bootc`, incremented once at boot —
  one NVS write per boot, negligible wear) mixed with the efuse MAC:
  `bootId = crc32(efuse_mac, 6) ^ (bootCounter * 0x9E3779B9u)`. Unique across boots and across
  devices; masked to 24 bits for display, stored full-width.
- `deviceHash`: `crc16(canonical deviceId)`. Stable for a given physical Tilt forever.
- `sequence`: `nextSequence++`, monotonic across all devices within the session. Derived on boot
  by scanning the newest segment for the highest sequence and adding 1 (see "Recovery" below), so
  it never restarts at a value already on flash.

**The ID is written to flash as part of the record and is never recomputed on retry** (§11).

## `ReadingQueue` API

```cpp
class ReadingQueue {
public:
    bool init();                                  // mkdir, scan, recover; false only on FS failure

    // --- producer, §9 ---
    // Snapshot order is mandatory: build -> persist -> only then attempt delivery.
    bool append(const QueuedReading &rec);        // fsync'd before returning true

    // --- consumer, §14/§15 ---
    // Fills up to maxRecords un-terminated records in FIFO order.
    size_t peekBatch(QueuedReading *out, size_t maxRecords);
    void   acknowledge(const char *recordId);     // journal kind=1
    void   acknowledgeAll(const char *const *ids, size_t count);
    void   flushJournal();                        // one fsync after a batch of acks

    // --- maintenance ---
    void   compact();                             // delete fully-terminated segments, rewrite journal
    bool   clear();                               // §23 "Clear queue" — delete every segment + journal

    // --- status, §23 ---
    size_t   pendingCount() const;
    uint32_t oldestPendingAgeSec() const;         // uses capturedAtUptimeMs when time is invalid
    uint32_t droppedOverflow() const;
    size_t   bytesUsed() const;
    uint8_t  storagePercent() const;              // bytesUsed / (maxQueuedRecords * 128)
    void     to_json(JsonDocument &doc) const;

private:
    uint32_t m_nextSequence = 1;
    uint32_t m_bootId = 0;
    uint32_t m_droppedOverflow = 0;
    uint32_t m_headSequence = 0;                  // all sequences <= this are terminated
    // in-RAM set of out-of-order terminated sequences above the head
    // bounded: at most one batch (20) can be non-prefix at a time, so a small sorted array is enough
};
extern ReadingQueue reading_queue;
```

### Append path

```
1. open newest segment "ab"; if it already holds QUEUE_RECORDS_PER_SEGMENT records, start a new one
2. fwrite(&rec, 1, 128, f)  -- a single 128 B write
3. fflush(f); fsync(fileno(f));  -- ★ required, LittleFS buffers otherwise
4. fclose(f)
5. on success: caller resets that device's RssiStats (see 05-rssi-aggregation.md)
```

★ Without `fsync` the record may live only in the LittleFS cache and be lost by a power cut,
which fails the §26 acceptance test. `fsync` on a 128 B append writes one block — this happens
twice an hour per device, so wear is trivial (§25).

### Capacity enforcement (§10)

Before appending, if `pendingCount() >= config.maxQueuedRecords`:

```
1. find the oldest un-terminated record
2. append journal entry kind=2 (dropped)
3. m_droppedOverflow++
4. Log.warning("QUEUE_OVERFLOW: dropped record %s (cap %u)", id, cap)
5. surface via /api/queue/ as droppedOverflow  -- never silently discard (§10)
```

Also guard on real free space independently of the record cap: query
`esp_littlefs_info(FILESYSTEM_PARTITION, &total, &used)` and refuse to append (logging a distinct
`QUEUE_FS_FULL` warning) when free space drops below 32 KB, so the queue can never make the
device unflashable or break config saves.

### Recovery on boot (§26)

```
1. mkdir -p /littlefs/queue
2. readdir, collect seg-*.bin, sort by number
3. for each segment, for each 128 B slot in order:
     - read; if fewer than 128 bytes remain -> truncated tail, stop this segment (and truncate it
       to the last whole record so future appends stay aligned)
     - if magic != QUEUE_RECORD_MAGIC or crc32 mismatch -> log, stop this segment
       (a bad record can only be the interrupted final write; everything before it is valid)
     - track maxSequence
4. m_nextSequence = maxSequence + 1
5. replay journal.bin in 8 B steps; ignore a trailing partial entry; build the terminated set
6. advance m_headSequence over the contiguous terminated prefix
7. compact()
```

Point 3's "stop this segment" is deliberately conservative: only the newest segment can have a
torn tail, so a mid-list corrupt record indicates real damage and losing the remainder of that one
8 KB segment is the safe response. Log it loudly and keep going with later segments.

## Snapshot production

New timer in `dataSendHandler`, independent of every cloud-logging timer (§8):

```cpp
TimerHandle_t queueSnapshotTimer;
bool snapshot_due = false;
void take_queue_snapshot();     // called from process(), under a SenderLock? NO — see below
```

`take_queue_snapshot()` writes flash but sends nothing, so it must **not** hold the sender lock —
otherwise a wedged HTTP request would also stop snapshots, which is the opposite of what §7 wants.
Call it from `process()` **before** the Wi-Fi gate:

```cpp
void dataSendHandler::process() {
    sender_health.heartbeat();
    if (snapshot_due) { snapshot_due = false; take_queue_snapshot(); }   // works offline (§7)
    if (!network_is_usable()) return;
    ... existing sends ...
    drain_queue_to_google();     // stage 10
}
```

`take_queue_snapshot()`:

```
tilt_scanner.drop_expired_tilts();
for each tiltHydrometer th:
    if (!device_config.isEnabled(th.deviceId())) continue;
    if (th has never reported a gravity) continue;          // don't queue zeroes
    build QueuedReading (zeroed, then filled, then crc32)
    if (reading_queue.append(rec)) th.rssi_stats.resetInterval();
startTimer(queueSnapshotTimer, config.queueSnapshotIntervalSec);
```

One record per configured Tilt per interval (§8). With four Tilts at 30 min that is 8 records/hour
= 1344 per week, matching the spec's arithmetic.

**Iteration safety**: `m_tilt_devices` is mutated by the BLE callback task. Today every consumer
iterates it from `loopTask` without a lock, and `take_queue_snapshot()` follows the same pattern —
so it inherits the existing (pre-existing) race rather than adding a new one. Note it as a known
limitation; adding a list mutex is a broader change than this phase should take on. Do **not**
iterate the list from the monitor task (see `03-sender-recovery.md`).

## Time and timestamps (§12)

There is **no SNTP client anywhere in the tree** (`grep -rn sntp src/` → nothing). `std::time(0)`
is currently used raw by brewstatus (`sendData.cpp:453`) and MQTT (`targets/mqtt.cpp:324`), which
means both are emitting 1970-based values today. Add the minimum needed:

### `src/time_sync.h` / `.cpp`

```cpp
void time_sync_init();          // esp_netif_sntp_init with pool.ntp.org, start after first GOT_IP
bool time_is_valid();           // true once a sync has landed
uint32_t utc_now();             // unix seconds, 0 when !time_is_valid()
void format_utc_iso8601(uint32_t unixSeconds, char *out, size_t outSize);  // "2026-08-10T08:40:00Z"
```

Implementation notes:
- `esp_netif_sntp_init()` with `ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org")`,
  `start = false`, then `esp_netif_sntp_start()` from the `WIFI_CFG_EVT_GOT_IP` handler in
  `wifi_setup.cpp:60`. Set the sync callback to latch a `bool g_time_valid`.
- Sync interval: leave the IDF default (1 h). Set `setenv("TZ", "UTC0", 1); tzset();` so
  `gmtime_r` and `localtime_r` agree — everything internal is UTC (§12).
- Sanity gate: treat a time as valid only when `> 1735689600` (2025-01-01), so a bogus early sync
  cannot mark records valid.
- Do not change brewstatus or MQTT behaviour in this phase beyond the fact that their
  `std::time(0)` now returns a real value once SNTP lands. That is a fix, not a regression — but
  call it out in the changelog because brewstatus timepoints will shift from 1970 to now.

### Record timestamping rules

```cpp
if (time_is_valid()) {
    rec.capturedAtUtc = utc_now();
    rec.flags |= QR_FLAG_TIMESTAMP_VALID;
} else {
    rec.capturedAtUtc = 0;                 // never fabricate (§12)
    // flag stays clear
}
rec.capturedAtUptimeMs = sh_millis();      // always, preserves ordering (§12)
```

`sequence` gives absolute ordering regardless, which is what §12 asks for ("preserve record
ordering"). Records queued before the first sync keep `timestampValid = false` permanently — the
server decides what to do with them (documented in `APPS_SCRIPT_PROTOCOL.md`).

Post-hoc back-filling of `capturedAtUtc` after a late sync is deliberately **not** done: it would
require rewriting records in place, which the append-only design forbids, and §12 prefers an
honest `false` over a computed guess.

## `/api/queue/` GET response (§23)

```json
{
  "queuedReadings": 37,
  "oldestReadingAgeSec": 15600,
  "storagePercent": 18,
  "bytesUsed": 34560,
  "maxRecords": 1500,
  "snapshotIntervalSec": 1800,
  "lastUploadSuccessAgeSec": 120,
  "uploadStatus": "IDLE",
  "droppedOverflow": 0,
  "timeValid": true,
  "recordSize": 128,
  "fsFreeBytes": 780000
}
```

`uploadStatus` ∈ `IDLE | SENDING | RETRYING | DISABLED` (`DISABLED` when the v2 Google mode is off).

## `/api/queue/actions/` POST (§23, optional controls)

```json
{"action": "sendBacklogNow"}     -> sets a flag; process() drains on the next pass
{"action": "clearQueue", "confirm": true}   -> requires confirm:true, else 400
```

The confirm field enforces §23's "must require explicit confirmation" on the server side as well
as in the UI.

## Flash-wear summary (§25)

| Write | Frequency | Size |
|---|---|---|
| Record append + fsync | 2/hour per device (8/hour at four Tilts) | 128 B (one block) |
| Journal append | once per acked record, batched fsync | 8 B |
| `state.bin` | once per snapshot interval | 24 B |
| Segment delete | ~once per 64 acked records | — |
| NVS boot counter | once per boot | tiny |

No per-advert writes, no RSSI persistence, no sender-diagnostic persistence except the one-shot
recovery record before a reboot.

**Build here.**
