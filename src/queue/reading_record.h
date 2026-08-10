#ifndef TILTBRIDGE_READING_RECORD_H
#define TILTBRIDGE_READING_RECORD_H

#include <cstddef>
#include <cstdint>

#define QUEUE_RECORD_MAGIC        0x51524231u   // 'QRB1'
#define QUEUE_RECORD_VERSION      1
#define QUEUE_RECORD_SIZE         128
#define QUEUE_RECORDS_PER_SEGMENT 64            // 64 * 128 = 8192 B per segment file

#define QR_FLAG_TIMESTAMP_VALID   0x01
#define QR_FLAG_TILT_PRO          0x02

#define QR_DEVICE_ID_LEN 18
#define QR_SHEET_NAME_LEN 26

// Formatted record id: "A91F2C-6A72-00000452" = 20 chars + NUL
#define QR_RECORD_ID_LEN 24

#pragma pack(push, 1)
/**
 * @brief One persisted reading. Exactly 128 bytes on flash.
 *
 * Self-contained by design: the sheet name is snapshotted at capture time so a record
 * delivered after the user renames a Tilt still lands where it was captured for. Written
 * with a single 128-byte append and never rewritten, so an interrupted write can only
 * damage the trailing record.
 */
struct QueuedReading {
    uint32_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint16_t reserved0;

    // Identity. Stable across retries and reboots while the record remains queued.
    uint32_t bootId;
    uint16_t deviceHash;
    uint32_t sequence;

    char     deviceId[QR_DEVICE_ID_LEN];
    uint8_t  colorIndex;
    char     sheetName[QR_SHEET_NAME_LEN];

    // Readings. `gravity` is the normal final TiltBridge value; the other two are the
    // pre-existing intermediates, exposed but not recomputed.
    float    tempF;
    float    gravity;
    float    gravityRaw;
    float    gravitySmoothed;

    int8_t   rssiLatest;
    int8_t   rssiAverage;
    int8_t   rssiMinimum;
    int8_t   rssiMaximum;
    uint16_t rssiSamples;

    uint32_t capturedAtUtc;         // unix seconds; 0 when no trustworthy clock
    uint32_t capturedAtUptimeMs;    // always set; preserves ordering when the above is 0

    // Fields above total 93 bytes; 93 + 31 + 4 (crc32) = 128. The static_assert below
    // is the real guard if any field changes.
    uint8_t  padding[31];
    uint32_t crc32;                 // over the first QUEUE_RECORD_SIZE - 4 bytes
};
#pragma pack(pop)

static_assert(sizeof(QueuedReading) == QUEUE_RECORD_SIZE,
              "QueuedReading must be exactly QUEUE_RECORD_SIZE bytes");

uint32_t qr_crc32(const void *data, size_t length);
uint16_t qr_crc16(const char *str);

// Fill in `crc32` over the rest of the struct.
void qr_finalize(QueuedReading &rec);

// True when magic, version and CRC all check out.
bool qr_is_valid(const QueuedReading &rec);

// "<bootId>-<deviceHash>-<sequence>", e.g. "A91F2C-6A72-00000452".
void qr_format_record_id(const QueuedReading &rec, char *out, size_t outSize);

#endif // TILTBRIDGE_READING_RECORD_H
