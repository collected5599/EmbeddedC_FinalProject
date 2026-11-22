#include "storage_manager.h"

#include "qspi_flash.h"

#include <stddef.h>
#include <string.h>

extern QSPI_HandleTypeDef hqspi;
extern IWDG_HandleTypeDef hiwdg;

#define STORAGE_MAGIC          0x434F4645u /* COFE */
#define STORAGE_LOG_BYTES      (QSPI_FLASH_EVENT_END - QSPI_FLASH_EVENT_BASE)
#define STORAGE_SLOTS_PER_SECTOR (QSPI_FLASH_SECTOR_BYTES / 32u)
#define STORAGE_TOTAL_SLOTS    (STORAGE_LOG_BYTES / 32u)

typedef struct __attribute__((packed)) {
    uint32_t magic;
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
    uint8_t reserved[6];
    uint16_t crc16;
} StorageRecord_t;

_Static_assert(sizeof(StorageRecord_t) == 32u, "QSPI log records must be 32 bytes");

static bool storage_available;
static bool scan_active;
static bool scan_found;
static uint32_t scan_slot;
static uint32_t scan_latest_sequence;
static uint32_t scan_latest_slot;
static uint32_t next_slot;
static uint32_t next_sequence;

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) :
                                    (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool record_is_erased(const StorageRecord_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t i;
    for (i = 0u; i < sizeof(*record); ++i) {
        if (bytes[i] != 0xFFu) return false;
    }
    return true;
}

static bool record_is_valid(const StorageRecord_t *record)
{
    return (record->magic == STORAGE_MAGIC) &&
           (record->crc16 == crc16_ccitt_false((const uint8_t *)record,
                                               offsetof(StorageRecord_t, crc16)));
}

static uint32_t slot_address(uint32_t slot)
{
    return QSPI_FLASH_EVENT_BASE + (slot * sizeof(StorageRecord_t));
}

StorageStatus_t Storage_BeginInit(void)
{
    uint32_t jedec_id;
    storage_available = false;
    scan_active = false;
    if (QspiFlash_Init(&hqspi, &jedec_id) != QSPI_FLASH_OK) {
        return STORAGE_UNAVAILABLE;
    }
    scan_found = false;
    scan_slot = 0u;
    scan_latest_sequence = 0u;
    scan_latest_slot = 0u;
    scan_active = true;
    return STORAGE_BUSY;
}

StorageStatus_t Storage_InitStep(uint32_t max_slots)
{
    StorageRecord_t record;
    uint32_t scanned = 0u;

    if (storage_available) return STORAGE_OK;
    if (!scan_active) return STORAGE_UNAVAILABLE;
    if (max_slots == 0u) return STORAGE_BUSY;

    while ((scan_slot < STORAGE_TOTAL_SLOTS) && (scanned < max_slots)) {
        if (QspiFlash_Read(slot_address(scan_slot), (uint8_t *)&record,
                           sizeof(record)) != QSPI_FLASH_OK) {
            scan_active = false;
            return STORAGE_IO_ERROR;
        }
        if (record_is_valid(&record) &&
            (!scan_found || (int32_t)(record.sequence - scan_latest_sequence) > 0)) {
            scan_found = true;
            scan_latest_sequence = record.sequence;
            scan_latest_slot = scan_slot;
        }
        ++scan_slot;
        ++scanned;
    }

    (void)HAL_IWDG_Refresh(&hiwdg);
    if (scan_slot < STORAGE_TOTAL_SLOTS) return STORAGE_BUSY;

    if (scan_found) {
        next_sequence = scan_latest_sequence + 1u;
        next_slot = (scan_latest_slot + 1u) % STORAGE_TOTAL_SLOTS;
    } else {
        next_sequence = 1u;
        next_slot = 0u;
    }
    scan_active = false;
    storage_available = true;
    return STORAGE_OK;
}

StorageStatus_t Storage_LogEvent(uint16_t event_id,
                                 uint32_t tick_ms,
                                 const CoffeeSmOutput_t *output,
                                 const CoffeeSmInput_t *input)
{
    StorageRecord_t record;
    StorageRecord_t verify;
    uint32_t address;
    QspiFlashStatus_t flash_status;
    uint8_t flags = 0u;

    if (!storage_available) return STORAGE_UNAVAILABLE;
    if ((output == NULL) || (input == NULL)) return STORAGE_IO_ERROR;

    address = slot_address(next_slot);
    if ((next_slot % STORAGE_SLOTS_PER_SECTOR) == 0u) {
        flash_status = QspiFlash_EraseSector(address);
        if (flash_status != QSPI_FLASH_OK) return STORAGE_IO_ERROR;
    } else {
        if (QspiFlash_Read(address, (uint8_t *)&verify, sizeof(verify)) != QSPI_FLASH_OK) {
            return STORAGE_IO_ERROR;
        }
        if (!record_is_erased(&verify)) {
            uint32_t sector_index = next_slot / STORAGE_SLOTS_PER_SECTOR;
            sector_index = (sector_index + 1u) %
                           (STORAGE_TOTAL_SLOTS / STORAGE_SLOTS_PER_SECTOR);
            next_slot = sector_index * STORAGE_SLOTS_PER_SECTOR;
            address = slot_address(next_slot);
            flash_status = QspiFlash_EraseSector(address);
            if (flash_status != QSPI_FLASH_OK) return STORAGE_IO_ERROR;
        }
    }

    memset(&record, 0xFF, sizeof(record));
    record.magic = STORAGE_MAGIC;
    record.sequence = next_sequence;
    record.tick_ms = tick_ms;
    record.event_id = event_id;
    record.state = (uint8_t)output->state;
    record.fault = (uint8_t)output->fault;
    record.temperature_x10 = (int16_t)(input->water_temp_c * 10.0f);
    record.cup_distance_mm = input->cup_sensor_valid ? input->cup_distance_mm : 0u;
    record.strength = (uint8_t)output->strength;
    if (input->water_temp_valid) flags |= (1u << 0);
    if (input->cup_sensor_valid) flags |= (1u << 1);
    if (input->cup_present) flags |= (1u << 2);
    record.flags = flags;
    record.heater_percent = output->heater_power_percent;
    record.pump_active = output->pump_active ? 1u : 0u;
    record.crc16 = crc16_ccitt_false((const uint8_t *)&record,
                                     offsetof(StorageRecord_t, crc16));

    flash_status = QspiFlash_ProgramPage(address, (const uint8_t *)&record,
                                         sizeof(record));
    if (flash_status != QSPI_FLASH_OK ||
        QspiFlash_Read(address, (uint8_t *)&verify, sizeof(verify)) != QSPI_FLASH_OK ||
        memcmp(&record, &verify, sizeof(record)) != 0) {
        return STORAGE_IO_ERROR;
    }
    ++next_sequence;
    next_slot = (next_slot + 1u) % STORAGE_TOTAL_SLOTS;
    return STORAGE_OK;
}

StorageStatus_t Storage_ReadLogSlot(uint32_t slot,
                                    StorageLogRecord_t *record,
                                    bool *valid)
{
    StorageRecord_t stored;

    /* A failed append disables further writes until rescan, but must not
     * prevent reading existing records while the QSPI interface is alive. */
    if (!QspiFlash_IsAvailable()) return STORAGE_UNAVAILABLE;
    if ((record == NULL) || (valid == NULL) || (slot >= STORAGE_TOTAL_SLOTS)) {
        return STORAGE_IO_ERROR;
    }
    *valid = false;
    if (QspiFlash_Read(slot_address(slot), (uint8_t *)&stored,
                       sizeof(stored)) != QSPI_FLASH_OK) {
        return STORAGE_IO_ERROR;
    }
    if (!record_is_valid(&stored)) return STORAGE_OK;

    record->sequence = stored.sequence;
    record->tick_ms = stored.tick_ms;
    record->event_id = stored.event_id;
    record->state = stored.state;
    record->fault = stored.fault;
    record->temperature_x10 = stored.temperature_x10;
    record->cup_distance_mm = stored.cup_distance_mm;
    record->strength = stored.strength;
    record->flags = stored.flags;
    record->heater_percent = stored.heater_percent;
    record->pump_active = stored.pump_active;
    record->crc16 = stored.crc16;
    *valid = true;
    return STORAGE_OK;
}

bool Storage_IsAvailable(void)
{
    return storage_available;
}
