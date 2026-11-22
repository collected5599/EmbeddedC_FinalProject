#include "tof_calibration_store.h"

#include "qspi_flash.h"

#include <stddef.h>
#include <string.h>

#define TOF_PROFILE_MAGIC   0x544F4643u /* TOFC */
#define TOF_PROFILE_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint32_t generation;
    uint16_t cup_present_mm;
    uint16_t cup_absent_mm;
    uint16_t clear_median_mm;
    uint16_t cup_median_mm;
    uint16_t removed_median_mm;
    uint8_t clear_valid_samples;
    uint8_t cup_valid_samples;
    uint8_t removed_valid_samples;
    uint8_t reserved[7];
    uint16_t crc16;
} TofStoredProfile_t;

_Static_assert(sizeof(TofStoredProfile_t) == 32u,
               "ToF calibration profile must be 32 bytes");

static uint16_t profile_crc16(const uint8_t *data, size_t length)
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

static bool profile_is_valid(const TofStoredProfile_t *stored)
{
    return stored->magic == TOF_PROFILE_MAGIC &&
           stored->version == TOF_PROFILE_VERSION &&
           stored->cup_present_mm > 0u &&
           stored->cup_present_mm < stored->cup_absent_mm &&
           stored->cup_absent_mm <= 2000u &&
           stored->crc16 == profile_crc16((const uint8_t *)stored,
                                           offsetof(TofStoredProfile_t, crc16));
}

static bool read_latest_profile(TofStoredProfile_t *latest, uint32_t *address,
                                bool *read_ok)
{
    TofStoredProfile_t slot_a;
    TofStoredProfile_t slot_b;
    bool valid_a;
    bool valid_b;
    *read_ok = false;
    if (QspiFlash_Read(QSPI_FLASH_CALIBRATION_SLOT_A,
                       (uint8_t *)&slot_a, sizeof(slot_a)) != QSPI_FLASH_OK ||
        QspiFlash_Read(QSPI_FLASH_CALIBRATION_SLOT_B,
                       (uint8_t *)&slot_b, sizeof(slot_b)) != QSPI_FLASH_OK) {
        return false;
    }
    *read_ok = true;
    valid_a = profile_is_valid(&slot_a);
    valid_b = profile_is_valid(&slot_b);
    if (!valid_a && !valid_b) return false;
    if (valid_a && (!valid_b ||
        (int32_t)(slot_a.generation - slot_b.generation) > 0)) {
        *latest = slot_a;
        *address = QSPI_FLASH_CALIBRATION_SLOT_A;
    } else {
        *latest = slot_b;
        *address = QSPI_FLASH_CALIBRATION_SLOT_B;
    }
    return true;
}

bool TofCalibrationStore_Load(TofCalibrationProfile_t *profile)
{
    TofStoredProfile_t stored;
    uint32_t address;
    bool read_ok;
    if ((profile == NULL) || !read_latest_profile(&stored, &address, &read_ok)) {
        return false;
    }
    (void)address;
    profile->generation = stored.generation;
    profile->cup_present_mm = stored.cup_present_mm;
    profile->cup_absent_mm = stored.cup_absent_mm;
    profile->clear_median_mm = stored.clear_median_mm;
    profile->cup_median_mm = stored.cup_median_mm;
    profile->removed_median_mm = stored.removed_median_mm;
    profile->clear_valid_samples = stored.clear_valid_samples;
    profile->cup_valid_samples = stored.cup_valid_samples;
    profile->removed_valid_samples = stored.removed_valid_samples;
    return true;
}

bool TofCalibrationStore_Save(const TofCalibrationProfile_t *profile)
{
    TofStoredProfile_t stored;
    TofStoredProfile_t verify;
    TofStoredProfile_t current;
    uint32_t current_address = 0u;
    uint32_t write_address;
    bool has_current;
    bool read_ok;

    if ((profile == NULL) || (profile->cup_present_mm == 0u) ||
        (profile->cup_present_mm >= profile->cup_absent_mm) ||
        (profile->cup_absent_mm > 2000u)) {
        return false;
    }
    memset(&stored, 0, sizeof(stored));
    stored.magic = TOF_PROFILE_MAGIC;
    stored.version = TOF_PROFILE_VERSION;
    has_current = read_latest_profile(&current, &current_address, &read_ok);
    if (!read_ok) return false;
    stored.generation = has_current ? current.generation + 1u : 1u;
    stored.cup_present_mm = profile->cup_present_mm;
    stored.cup_absent_mm = profile->cup_absent_mm;
    stored.clear_median_mm = profile->clear_median_mm;
    stored.cup_median_mm = profile->cup_median_mm;
    stored.removed_median_mm = profile->removed_median_mm;
    stored.clear_valid_samples = profile->clear_valid_samples;
    stored.cup_valid_samples = profile->cup_valid_samples;
    stored.removed_valid_samples = profile->removed_valid_samples;
    stored.crc16 = profile_crc16((const uint8_t *)&stored,
                                 offsetof(TofStoredProfile_t, crc16));

    write_address = !has_current ? QSPI_FLASH_CALIBRATION_SLOT_A :
        (current_address == QSPI_FLASH_CALIBRATION_SLOT_A ?
         QSPI_FLASH_CALIBRATION_SLOT_B : QSPI_FLASH_CALIBRATION_SLOT_A);
    if (QspiFlash_EraseSector(write_address) != QSPI_FLASH_OK ||
        QspiFlash_ProgramPage(write_address,
                              (const uint8_t *)&stored, sizeof(stored)) != QSPI_FLASH_OK ||
        QspiFlash_Read(write_address,
                      (uint8_t *)&verify, sizeof(verify)) != QSPI_FLASH_OK) {
        return false;
    }
    return (memcmp(&stored, &verify, sizeof(stored)) == 0) &&
           profile_is_valid(&verify);
}
