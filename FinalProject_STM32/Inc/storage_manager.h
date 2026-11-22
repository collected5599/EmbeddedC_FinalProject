#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "coffee_sm.h"
#include "qspi_flash.h"

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_LOG_RECORD_BYTES 32u
#define STORAGE_LOG_CAPACITY \
    ((QSPI_FLASH_EVENT_END - QSPI_FLASH_EVENT_BASE) / STORAGE_LOG_RECORD_BYTES)

typedef enum {
    STORAGE_OK = 0,
    STORAGE_UNAVAILABLE,
    STORAGE_CORRUPT,
    STORAGE_IO_ERROR,
    STORAGE_BUSY
} StorageStatus_t;

#define STORAGE_EVENT_BOOT 0x0100u

typedef struct {
    uint32_t sequence;
    uint32_t tick_ms;
    uint16_t event_id;
    uint8_t state;
    uint8_t fault;
    int16_t temperature_x10;
    uint16_t cup_distance_mm;
    uint8_t strength;
    uint8_t flags;
    uint8_t heater_percent;
    uint8_t pump_active;
    uint16_t crc16;
} StorageLogRecord_t;

StorageStatus_t Storage_BeginInit(void);
StorageStatus_t Storage_InitStep(uint32_t max_slots);
StorageStatus_t Storage_LogEvent(uint16_t event_id,
                                 uint32_t tick_ms,
                                 const CoffeeSmOutput_t *output,
                                 const CoffeeSmInput_t *input);
StorageStatus_t Storage_ReadLogSlot(uint32_t slot,
                                    StorageLogRecord_t *record,
                                    bool *valid);
bool Storage_IsAvailable(void);

#endif /* STORAGE_MANAGER_H */
