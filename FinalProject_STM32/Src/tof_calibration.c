#include "tof_calibration.h"

#include "main.h"
#include "qspi_flash.h"
#include "tof_calibration_store.h"
#include "vl53l0x_api.h"
#include "vl53l0x_tof.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c2;
extern IWDG_HandleTypeDef hiwdg;
extern QSPI_HandleTypeDef hqspi;
extern UART_HandleTypeDef huart1;

#define TOF_CAL_ADDRESS          (0x29u << 1)
#define TOF_CAL_SAMPLE_COUNT     10u
#define TOF_CAL_SAMPLE_PERIOD_MS 150u
#define TOF_CAL_SETTLE_MS        1000u
#define TOF_CAL_MIN_OK_SAMPLES   8u
#define TOF_CAL_MAX_MM           2000u

typedef enum {
    CAL_WAIT_CLEAR_SETTLE = 0,
    CAL_CAPTURE_CLEAR,
    CAL_WAIT_PLACE,
    CAL_CAPTURE_PLACE,
    CAL_WAIT_REMOVE,
    CAL_CAPTURE_REMOVE,
    CAL_DONE,
    CAL_FAILED
} CalibrationState_t;

typedef struct {
    uint8_t api_ok;
    uint8_t range_status;
    uint16_t range_mm;
} CalibrationSample_t;

static VL53L0X_Dev_t sensor;
static CalibrationSample_t clear_samples[TOF_CAL_SAMPLE_COUNT];
static CalibrationSample_t place_samples[TOF_CAL_SAMPLE_COUNT];
static CalibrationSample_t remove_samples[TOF_CAL_SAMPLE_COUNT];
static CalibrationState_t state;
static uint32_t next_sample_tick;
static uint8_t sample_index;
static bool qspi_ready;
static TofCalibrationProfile_t prior_profile;
static bool prior_profile_valid;
static bool discard_lf_after_cr;

static void uart_line(const char *line)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                            (uint16_t)strlen(line), 1000u);
}

static bool sensor_init(void)
{
    uint8_t vhv_settings;
    uint8_t phase_cal;
    uint8_t aperture_spads;
    uint32_t ref_spad_count;
    VL53L0X_Error result;

    memset(&sensor, 0, sizeof(sensor));
    sensor.I2cHandle = &hi2c2;
    sensor.I2cDevAddr = TOF_CAL_ADDRESS;
    sensor.Present = 1u;
    sensor.comms_type = 1u;
    sensor.comms_speed_khz = 100u;

    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, VL53L0X_XSHUT_Pin, GPIO_PIN_RESET);
    HAL_Delay(10u);
    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, VL53L0X_XSHUT_Pin, GPIO_PIN_SET);
    HAL_Delay(10u);
    if (HAL_I2C_IsDeviceReady(&hi2c2, TOF_CAL_ADDRESS, 2u, 20u) != HAL_OK) {
        return false;
    }

    result = VL53L0X_DataInit(&sensor);
    if (result == VL53L0X_ERROR_NONE) result = VL53L0X_StaticInit(&sensor);
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_PerformRefCalibration(&sensor, &vhv_settings, &phase_cal);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_PerformRefSpadManagement(&sensor, &ref_spad_count,
                                                   &aperture_spads);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetDeviceMode(&sensor, VL53L0X_DEVICEMODE_SINGLE_RANGING);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetLimitCheckEnable(&sensor,
                   VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 1u);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetLimitCheckEnable(&sensor,
                   VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 1u);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetLimitCheckValue(&sensor,
                   VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE,
                   (FixPoint1616_t)(0.1f * 65536.0f));
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetLimitCheckValue(&sensor,
                   VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE,
                   (FixPoint1616_t)(60.0f * 65536.0f));
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&sensor, 33000u);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetVcselPulsePeriod(&sensor,
                   VL53L0X_VCSEL_PERIOD_PRE_RANGE, 18u);
    }
    if (result == VL53L0X_ERROR_NONE) {
        result = VL53L0X_SetVcselPulsePeriod(&sensor,
                   VL53L0X_VCSEL_PERIOD_FINAL_RANGE, 14u);
    }
    return result == VL53L0X_ERROR_NONE;
}

static bool sample_has_range(const CalibrationSample_t *sample)
{
    return sample->api_ok && sample->range_status == 0u &&
           sample->range_mm > 0u && sample->range_mm <= TOF_CAL_MAX_MM;
}

static uint16_t median_for_samples(const CalibrationSample_t *samples,
                                   uint8_t count, uint8_t *valid_count)
{
    uint16_t values[TOF_CAL_SAMPLE_COUNT * 2u];
    uint8_t used = 0u;
    uint8_t i;
    for (i = 0u; i < count; ++i) {
        if (sample_has_range(&samples[i])) values[used++] = samples[i].range_mm;
    }
    *valid_count = used;
    if (used == 0u) return 0u;
    for (i = 1u; i < used; ++i) {
        uint16_t value = values[i];
        uint8_t j = i;
        while (j > 0u && values[j - 1u] > value) {
            values[j] = values[j - 1u];
            --j;
        }
        values[j] = value;
    }
    if ((used & 1u) != 0u) return values[used / 2u];
    return (uint16_t)(((uint32_t)values[used / 2u - 1u] + values[used / 2u]) / 2u);
}

static uint8_t api_success_count(const CalibrationSample_t *samples)
{
    uint8_t count = 0u;
    uint8_t i;
    for (i = 0u; i < TOF_CAL_SAMPLE_COUNT; ++i) {
        if (samples[i].api_ok) ++count;
    }
    return count;
}

static uint8_t detected_count(const CalibrationSample_t *samples,
                              uint16_t present_threshold_mm)
{
    uint8_t count = 0u;
    uint8_t i;
    for (i = 0u; i < TOF_CAL_SAMPLE_COUNT; ++i) {
        if (sample_has_range(&samples[i]) &&
            samples[i].range_mm < present_threshold_mm) ++count;
    }
    return count;
}

static uint8_t absent_count(const CalibrationSample_t *samples,
                            uint16_t absent_threshold_mm)
{
    uint8_t count = 0u;
    uint8_t i;
    for (i = 0u; i < TOF_CAL_SAMPLE_COUNT; ++i) {
        if (samples[i].api_ok &&
            (!sample_has_range(&samples[i]) ||
             samples[i].range_mm >= absent_threshold_mm)) ++count;
    }
    return count;
}

static void capture_one(CalibrationSample_t *sample, const char *stage)
{
    VL53L0X_RangingMeasurementData_t measurement;
    char line[128];
    VL53L0X_Error result = VL53L0X_PerformSingleRangingMeasurement(&sensor,
                                                                    &measurement);
    sample->api_ok = (result == VL53L0X_ERROR_NONE) ? 1u : 0u;
    sample->range_status = sample->api_ok ? measurement.RangeStatus : 255u;
    sample->range_mm = sample->api_ok ? measurement.RangeMilliMeter : 0u;
    (void)snprintf(line, sizeof(line),
        "TOF_SAMPLE,stage=%s,index=%u,api=%s,status=%u,mm=%u\r\n",
        stage, (unsigned)(sample_index + 1u),
        sample->api_ok ? "OK" : "ERR", (unsigned)sample->range_status,
        (unsigned)sample->range_mm);
    uart_line(line);
}

static void finish_stage(const char *stage, const CalibrationSample_t *samples)
{
    char line[128];
    char median_text[8];
    uint8_t valid_count;
    uint16_t median = median_for_samples(samples, TOF_CAL_SAMPLE_COUNT,
                                         &valid_count);
    if (valid_count == 0u) {
        (void)snprintf(median_text, sizeof(median_text), "NA");
    } else {
        (void)snprintf(median_text, sizeof(median_text), "%u", (unsigned)median);
    }
    (void)snprintf(line, sizeof(line),
        "TOF_STAGE,stage=%s,api_ok=%u/%u,range_ok=%u/%u,median_mm=%s\r\n",
        stage, (unsigned)api_success_count(samples), TOF_CAL_SAMPLE_COUNT,
        (unsigned)valid_count, TOF_CAL_SAMPLE_COUNT, median_text);
    uart_line(line);
}

static bool build_and_save_profile(void)
{
    TofCalibrationProfile_t profile;
    uint8_t valid_clear;
    uint8_t valid_place;
    uint8_t valid_remove;
    uint16_t clear_median;
    uint16_t place_median;
    uint16_t remove_median;
    uint16_t clear_reference;
    uint16_t gap;
    uint8_t clear_absent;
    uint8_t place_detected;
    uint8_t remove_absent;
    bool passed;
    const char *threshold_basis;
    char line[256];

    clear_median = median_for_samples(clear_samples, TOF_CAL_SAMPLE_COUNT,
                                      &valid_clear);
    place_median = median_for_samples(place_samples, TOF_CAL_SAMPLE_COUNT,
                                      &valid_place);
    remove_median = median_for_samples(remove_samples, TOF_CAL_SAMPLE_COUNT,
                                       &valid_remove);

    if (valid_place < TOF_CAL_MIN_OK_SAMPLES || place_median == 0u) {
        uart_line("TOF_CAL_RESULT,FAIL=PLACE_NOT_RANGEABLE\r\n");
        return false;
    }
    if (valid_clear == 0u && valid_remove == 0u) {
        clear_reference = (uint16_t)(place_median + 60u);
        threshold_basis = "CUP_PLUS_60MM_FALLBACK";
    } else if (valid_clear == 0u) {
        clear_reference = remove_median;
        threshold_basis = "REMOVED_STAGE_MEDIAN";
    } else if (valid_remove == 0u) {
        clear_reference = clear_median;
        threshold_basis = "CLEAR_STAGE_MEDIAN";
    } else {
        clear_reference = (uint16_t)(((uint32_t)clear_median + remove_median) / 2u);
        threshold_basis = "CLEAR_REMOVED_MEDIAN_AVERAGE";
    }
    if (clear_reference <= place_median + 20u) {
        uart_line("TOF_CAL_RESULT,FAIL=CUP_AND_CLEAR_NOT_SEPARATED\r\n");
        return false;
    }

    gap = (uint16_t)(clear_reference - place_median);
    if (gap > 180u) gap = 180u;
    profile.cup_present_mm = (uint16_t)(place_median + gap / 3u);
    profile.cup_absent_mm = (uint16_t)(place_median + (2u * gap) / 3u);
    if (profile.cup_absent_mm <= profile.cup_present_mm) {
        profile.cup_absent_mm = (uint16_t)(profile.cup_present_mm + 1u);
    }
    if (profile.cup_absent_mm > TOF_CAL_MAX_MM) {
        uart_line("TOF_CAL_RESULT,FAIL=THRESHOLD_OUT_OF_RANGE\r\n");
        return false;
    }

    profile.clear_median_mm = clear_median;
    profile.cup_median_mm = place_median;
    profile.removed_median_mm = remove_median;
    profile.generation = 0u; /* The store assigns the next persistent generation. */
    profile.clear_valid_samples = valid_clear;
    profile.cup_valid_samples = valid_place;
    profile.removed_valid_samples = valid_remove;
    clear_absent = absent_count(clear_samples, profile.cup_absent_mm);
    place_detected = detected_count(place_samples, profile.cup_present_mm);
    remove_absent = absent_count(remove_samples, profile.cup_absent_mm);
    passed = (api_success_count(clear_samples) >= TOF_CAL_MIN_OK_SAMPLES) &&
             (api_success_count(place_samples) >= TOF_CAL_MIN_OK_SAMPLES) &&
             (api_success_count(remove_samples) >= TOF_CAL_MIN_OK_SAMPLES) &&
             (clear_absent >= TOF_CAL_MIN_OK_SAMPLES) &&
             (place_detected >= TOF_CAL_MIN_OK_SAMPLES) &&
             (remove_absent >= TOF_CAL_MIN_OK_SAMPLES);
    if (!passed) {
        (void)snprintf(line, sizeof(line),
            "TOF_CAL_RESULT,FAIL,clear_absent=%u/%u,cup_detect=%u/%u,removed_absent=%u/%u\r\n",
            (unsigned)clear_absent, TOF_CAL_SAMPLE_COUNT,
            (unsigned)place_detected, TOF_CAL_SAMPLE_COUNT,
            (unsigned)remove_absent, TOF_CAL_SAMPLE_COUNT);
        uart_line(line);
        return false;
    }
    if (!qspi_ready || !TofCalibrationStore_Save(&profile)) {
        uart_line("TOF_CAL_RESULT,FAIL=QSPI_SAVE_ERROR\r\n");
        return false;
    }

    (void)snprintf(line, sizeof(line),
        "TOF_CAL_RESULT,PASS,saved=QSPI_SLOTS_1_2,threshold_basis=%s,clear_range_ok=%u/%u,cup_median_mm=%u,removed_range_ok=%u/%u,present_below_mm=%u,absent_at_or_above_mm=%u\r\n",
        threshold_basis, (unsigned)valid_clear, TOF_CAL_SAMPLE_COUNT,
        (unsigned)place_median, (unsigned)valid_remove, TOF_CAL_SAMPLE_COUNT,
        (unsigned)profile.cup_present_mm, (unsigned)profile.cup_absent_mm);
    uart_line(line);
    return true;
}

static void begin_capture(CalibrationState_t capture_state)
{
    state = capture_state;
    sample_index = 0u;
    next_sample_tick = HAL_GetTick();
}

static bool wait_for_enter(void)
{
    uint8_t byte;
    while (HAL_UART_Receive(&huart1, &byte, 1u, 0u) == HAL_OK) {
        if (byte == '\r') {
            discard_lf_after_cr = true;
            return true;
        }
        if (byte == '\n') {
            if (discard_lf_after_cr) {
                discard_lf_after_cr = false;
                continue;
            }
            return true;
        }
        discard_lf_after_cr = false;
    }
    return false;
}

static void capture_stage(const char *name, CalibrationSample_t *samples,
                          CalibrationState_t next_state)
{
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - next_sample_tick) < 0) return;
    next_sample_tick = now + TOF_CAL_SAMPLE_PERIOD_MS;
    capture_one(&samples[sample_index], name);
    ++sample_index;
    (void)HAL_IWDG_Refresh(&hiwdg);
    if (sample_index < TOF_CAL_SAMPLE_COUNT) return;

    finish_stage(name, samples);
    if (next_state == CAL_WAIT_PLACE) {
        uart_line("TOF_CAL_PROMPT,PLACE_CUP,position cup at normal detect location then press ENTER\r\n");
    } else if (next_state == CAL_WAIT_REMOVE) {
        uart_line("TOF_CAL_PROMPT,REMOVE_CUP,clear the sensor view like the initial stage then press ENTER\r\n");
    } else {
        (void)build_and_save_profile();
        state = CAL_DONE;
        return;
    }
    state = next_state;
}

void TofCalibration_Init(void)
{
    uint32_t jedec_id = 0u;
    char line[160];
    qspi_ready = (QspiFlash_Init(&hqspi, &jedec_id) == QSPI_FLASH_OK);
    prior_profile_valid = qspi_ready && TofCalibrationStore_Load(&prior_profile);

    uart_line("\r\nTOF_CAL,mode=SINGLE_RANGING,samples=10\r\n");
    uart_line("TOF_CAL_PROMPT,CLEAR,remove all objects from the sensor view; clear capture starts in 1 second\r\n");
    (void)snprintf(line, sizeof(line), "TOF_CAL_QSPI,%s,id=%06lX,prior_profile=%s\r\n",
        qspi_ready ? "READY" : "UNAVAILABLE", (unsigned long)jedec_id,
        prior_profile_valid ? "YES" : "NO");
    uart_line(line);

    if (!sensor_init()) {
        uart_line("TOF_CAL_ERROR,SENSOR_INIT_FAILED,check I2C2_and_XSHUT\r\n");
        state = CAL_FAILED;
        return;
    }
    memset(clear_samples, 0, sizeof(clear_samples));
    memset(place_samples, 0, sizeof(place_samples));
    memset(remove_samples, 0, sizeof(remove_samples));
    state = CAL_WAIT_CLEAR_SETTLE;
    next_sample_tick = HAL_GetTick() + TOF_CAL_SETTLE_MS;
}

void TofCalibration_Task(void)
{
    if (state == CAL_WAIT_CLEAR_SETTLE &&
        (int32_t)(HAL_GetTick() - next_sample_tick) >= 0) {
        uart_line("TOF_CAL_STAGE,CLEAR,no_object\r\n");
        begin_capture(CAL_CAPTURE_CLEAR);
    } else if (state == CAL_CAPTURE_CLEAR) {
        capture_stage("CLEAR", clear_samples, CAL_WAIT_PLACE);
    } else if (state == CAL_WAIT_PLACE && wait_for_enter()) {
        begin_capture(CAL_CAPTURE_PLACE);
        uart_line("TOF_CAL_STAGE,PLACE,capturing\r\n");
    } else if (state == CAL_CAPTURE_PLACE) {
        capture_stage("PLACE", place_samples, CAL_WAIT_REMOVE);
    } else if (state == CAL_WAIT_REMOVE && wait_for_enter()) {
        begin_capture(CAL_CAPTURE_REMOVE);
        uart_line("TOF_CAL_STAGE,REMOVE,capturing\r\n");
    } else if (state == CAL_CAPTURE_REMOVE) {
        capture_stage("REMOVE", remove_samples, CAL_DONE);
    }
}
