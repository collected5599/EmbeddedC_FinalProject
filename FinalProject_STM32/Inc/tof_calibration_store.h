#ifndef TOF_CALIBRATION_STORE_H
#define TOF_CALIBRATION_STORE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation;
    uint16_t cup_present_mm;
    uint16_t cup_absent_mm;
    uint16_t clear_median_mm;
    uint16_t cup_median_mm;
    uint16_t removed_median_mm;
    uint8_t clear_valid_samples;
    uint8_t cup_valid_samples;
    uint8_t removed_valid_samples;
} TofCalibrationProfile_t;

/* QSPI must have been initialized before calling either function. */
bool TofCalibrationStore_Load(TofCalibrationProfile_t *profile);
bool TofCalibrationStore_Save(const TofCalibrationProfile_t *profile);

#endif /* TOF_CALIBRATION_STORE_H */
