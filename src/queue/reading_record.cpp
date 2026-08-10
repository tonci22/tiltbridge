#include <cstdio>
#include <cstring>

#include "reading_record.h"

// Standard CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Computed bitwise rather than
// from a 1 KB table: this runs a handful of times per hour, so RAM matters more than speed.
uint32_t qr_crc32(const void *data, size_t length) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    while (length--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

// CRC-16/CCITT-FALSE over a NUL-terminated string. Used to derive the short, stable
// per-device component of a record id.
uint16_t qr_crc16(const char *str) {
    if (str == nullptr)
        return 0;

    uint16_t crc = 0xFFFF;
    for (const char *p = str; *p != '\0'; p++) {
        crc ^= (uint16_t)((uint8_t)*p) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void qr_finalize(QueuedReading &rec) {
    rec.magic = QUEUE_RECORD_MAGIC;
    rec.version = QUEUE_RECORD_VERSION;
    rec.crc32 = qr_crc32(&rec, QUEUE_RECORD_SIZE - sizeof(uint32_t));
}

bool qr_is_valid(const QueuedReading &rec) {
    if (rec.magic != QUEUE_RECORD_MAGIC)
        return false;
    if (rec.version != QUEUE_RECORD_VERSION)
        return false;

    return rec.crc32 == qr_crc32(&rec, QUEUE_RECORD_SIZE - sizeof(uint32_t));
}

void qr_format_record_id(const QueuedReading &rec, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return;

    snprintf(out, outSize, "%06lX-%04X-%08lX",
             (unsigned long)(rec.bootId & 0x00FFFFFFu),
             (unsigned)rec.deviceHash,
             (unsigned long)rec.sequence);
}
