#include "coffee_app.h"

#include "coffee_sm.h"
#include "main.h"
#include "storage_manager.h"
#include "tof_calibration_store.h"
#include "vl53l0x_api.h"
#include "vl53l0x_tof.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c2;
extern IWDG_HandleTypeDef hiwdg;
extern UART_HandleTypeDef huart1;

#define APP_DEBOUNCE_MS             30u
#define APP_REPORT_PERIOD_MS       100u
#define APP_TOF_PERIOD_MS          100u
#define APP_TOF_RETRY_PERIOD_MS   2000u
#define APP_STORAGE_RETRY_PERIOD_MS 5000u
#define APP_QSPI_SCAN_SLOTS_PER_TASK 8u
#define APP_CUP_PRESENT_DEFAULT_MM  80u
#define APP_CUP_ABSENT_DEFAULT_MM  100u
#define APP_TEMP_MIN_C             20.0f
#define APP_TEMP_MAX_C            110.0f
#define APP_TOF_ADDRESS             (0x29u << 1)

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState raw;
    GPIO_PinState stable;
    uint32_t changed_at;
    bool pressed_event;
} DebouncedInput_t;

static CoffeeSm_t coffee_sm;
static CoffeeSmInput_t sm_input;
static CoffeeSmOutput_t sm_output;
static VL53L0X_Dev_t tof_device;
static DebouncedInput_t power_button;
static DebouncedInput_t start_button;
static DebouncedInput_t strength_switch;
static uint32_t last_task_tick;
static uint32_t last_tof_tick;
static uint32_t last_tof_retry_tick;
static uint32_t last_report_tick;
static uint32_t last_storage_retry_tick;
static uint16_t filtered_adc;
static uint16_t cup_distance_mm;
static uint8_t tof_error_count;
static uint16_t cup_present_threshold_mm = APP_CUP_PRESENT_DEFAULT_MM;
static uint16_t cup_absent_threshold_mm = APP_CUP_ABSENT_DEFAULT_MM;
static bool adc_valid;
static bool tof_valid;
static bool cup_present;
static bool tof_initialized;
static bool storage_available;
static bool storage_init_in_progress;
static bool storage_boot_log_pending;
static char uart_command[32];
static uint8_t uart_command_length;
static bool uart_command_overflow;
static bool log_stream_active;
static bool log_scan_active;
static bool log_scan_found_latest;
static uint32_t log_scan_slot;
static uint32_t log_scan_count;
static uint32_t log_stream_slot;
static uint32_t log_stream_max_sequence;
static uint32_t log_stream_sent_count;

static void input_init(DebouncedInput_t *input, GPIO_TypeDef *port,
                       uint16_t pin, uint32_t now)
{
    input->port = port;
    input->pin = pin;
    input->raw = HAL_GPIO_ReadPin(port, pin);
    input->stable = input->raw;
    input->changed_at = now;
    input->pressed_event = false;
}

static void input_update(DebouncedInput_t *input, uint32_t now,
                         bool active_low)
{
    GPIO_PinState sample = HAL_GPIO_ReadPin(input->port, input->pin);
    input->pressed_event = false;
    if (sample != input->raw) {
        input->raw = sample;
        input->changed_at = now;
    }
    if ((input->stable != input->raw) &&
        ((uint32_t)(now - input->changed_at) >= APP_DEBOUNCE_MS)) {
        input->stable = input->raw;
        input->pressed_event = active_low ? (input->stable == GPIO_PIN_RESET) :
                                           (input->stable == GPIO_PIN_SET);
    }
}

static bool read_adc(uint16_t *value)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return false;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return false;
    }
    *value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    return HAL_ADC_Stop(&hadc1) == HAL_OK;
}

static bool tof_init(void)
{
    uint8_t vhv_settings;
    uint8_t phase_cal;
    uint8_t is_aperture_spads;
    uint32_t ref_spad_count;
    VL53L0X_Error status;

    memset(&tof_device, 0, sizeof(tof_device));
    tof_device.I2cHandle = &hi2c2;
    tof_device.I2cDevAddr = APP_TOF_ADDRESS;
    tof_device.Present = 1;
    tof_device.comms_type = 1u;
    tof_device.comms_speed_khz = 100u;

    /* Reset the sensor on every initialization attempt so a bus or device
     * fault can recover without requiring an MCU reset. */
    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, VL53L0X_XSHUT_Pin, GPIO_PIN_RESET);
    HAL_Delay(10u);
    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, VL53L0X_XSHUT_Pin, GPIO_PIN_SET);
    HAL_Delay(10u);
    if (HAL_I2C_IsDeviceReady(&hi2c2, APP_TOF_ADDRESS, 2u, 20u) != HAL_OK) {
        return false;
    }
    status = VL53L0X_DataInit(&tof_device);
    if (status == VL53L0X_ERROR_NONE) status = VL53L0X_StaticInit(&tof_device);
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_PerformRefCalibration(&tof_device, &vhv_settings, &phase_cal);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_PerformRefSpadManagement(&tof_device, &ref_spad_count,
                                                  &is_aperture_spads);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetDeviceMode(&tof_device, VL53L0X_DEVICEMODE_SINGLE_RANGING);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetLimitCheckEnable(&tof_device,
                    VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 1u);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetLimitCheckEnable(&tof_device,
                    VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 1u);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetLimitCheckValue(&tof_device,
                    VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE,
                    (FixPoint1616_t)(0.1f * 65536.0f));
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetLimitCheckValue(&tof_device,
                    VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE,
                    (FixPoint1616_t)(60.0f * 65536.0f));
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&tof_device, 33000u);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetVcselPulsePeriod(&tof_device,
                    VL53L0X_VCSEL_PERIOD_PRE_RANGE, 18u);
    }
    if (status == VL53L0X_ERROR_NONE) {
        status = VL53L0X_SetVcselPulsePeriod(&tof_device,
                    VL53L0X_VCSEL_PERIOD_FINAL_RANGE, 14u);
    }
    return status == VL53L0X_ERROR_NONE;
}

static void read_tof(uint32_t now)
{
    VL53L0X_RangingMeasurementData_t measurement;
    VL53L0X_Error range_result;
    bool cup_was_present = cup_present;
    if ((uint32_t)(now - last_tof_tick) < APP_TOF_PERIOD_MS) return;
    last_tof_tick = now;
    tof_valid = false;
    cup_distance_mm = 0u;
    cup_present = false;
    if (!tof_initialized) {
        if ((uint32_t)(now - last_tof_retry_tick) < APP_TOF_RETRY_PERIOD_MS) return;
        last_tof_retry_tick = now;
        tof_initialized = tof_init();
        if (!tof_initialized) {
            if (tof_error_count < 3u) tof_error_count = 3u;
            return;
        }
        tof_error_count = 0u;
    }

    range_result = VL53L0X_PerformSingleRangingMeasurement(&tof_device,
                                                            &measurement);
    if (range_result == VL53L0X_ERROR_NONE) {
        /* A successful API transaction with a nonzero RangeStatus means the
         * sensor ran but did not produce a usable target distance. Do not
         * label that as an I2C communication failure. */
        tof_error_count = 0u;
        if ((measurement.RangeStatus == 0u) &&
            (measurement.RangeMilliMeter > 0u) &&
            (measurement.RangeMilliMeter <= 2000u)) {
            cup_distance_mm = measurement.RangeMilliMeter;
            tof_valid = true;
            if (cup_distance_mm < cup_present_threshold_mm) cup_present = true;
            else if (cup_distance_mm >= cup_absent_threshold_mm) cup_present = false;
            else cup_present = cup_was_present;
        }
    } else if (tof_error_count < UINT8_MAX) {
        ++tof_error_count;
        if (tof_error_count >= 3u) {
            tof_initialized = false;
            last_tof_retry_tick = now;
        }
    }
}

static CoffeeStrength_t selected_strength(void)
{
    return (strength_switch.stable == GPIO_PIN_RESET) ?
           COFFEE_STRENGTH_STRONG : COFFEE_STRENGTH_NORMAL;
}

static void write_pattern(GPIO_TypeDef *port, uint16_t pin,
                          CoffeeLedPattern_t pattern, uint32_t now)
{
    bool on = false;
    switch (pattern) {
    case COFFEE_LED_ON: on = true; break;
    case COFFEE_LED_BLINK_1HZ: on = ((now / 500u) & 1u) == 0u; break;
    case COFFEE_LED_BLINK_4HZ: on = ((now / 125u) & 1u) == 0u; break;
    case COFFEE_LED_OFF:
    default: break;
    }
    HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool pattern_is_on(CoffeeLedPattern_t pattern, uint32_t now)
{
    switch (pattern) {
    case COFFEE_LED_ON: return true;
    case COFFEE_LED_BLINK_1HZ: return ((now / 500u) & 1u) == 0u;
    case COFFEE_LED_BLINK_4HZ: return ((now / 125u) & 1u) == 0u;
    case COFFEE_LED_OFF:
    default: return false;
    }
}

static const char *state_name(CoffeeState_t state)
{
    static const char *const names[] = {
        "STANDBY", "IDLE", "HEATING", "BREWING", "COMPLETE", "FAULT"
    };
    return ((unsigned)state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "UNKNOWN";
}

static const char *fault_name(CoffeeFault_t fault)
{
    static const char *const names[] = {
        "NONE", "NO_CUP", "OVERTEMP", "HEAT_TIMEOUT", "SENSOR_COMM", "TANK_EMPTY"
    };
    return ((unsigned)fault < (sizeof(names) / sizeof(names[0]))) ? names[fault] : "UNKNOWN";
}

static void send_status(uint32_t now, float temperature_c)
{
    char line[320];
    int length;
    if ((uint32_t)(now - last_report_tick) < APP_REPORT_PERIOD_MS) return;
    last_report_tick = now;
    length = snprintf(line, sizeof(line),
        "STATUS,state=%s,temp_x10=%u,target_x10=%u,cup_mm=%u,cup_valid=%u,cup=%u,cup_enter_mm=%u,cup_exit_mm=%u,strength=%s,heater=%u,pump=%u,ready_led=%u,heating_led=%u,fault_led=%u,heartbeat_led=%u,flash=%s,logger=%s,fault=%s\r\n",
        state_name(sm_output.state), (unsigned)(temperature_c * 10.0f),
        (unsigned)(sm_output.target_temp_c * 10.0f),
        (unsigned)cup_distance_mm, tof_valid ? 1u : 0u, cup_present ? 1u : 0u,
        (unsigned)cup_present_threshold_mm, (unsigned)cup_absent_threshold_mm,
        sm_output.strength == COFFEE_STRENGTH_STRONG ? "STRONG" : "NORMAL",
        (unsigned)sm_output.heater_power_percent, sm_output.pump_active ? 1u : 0u,
        pattern_is_on(sm_output.ready_led, now) ? 1u : 0u,
        pattern_is_on(sm_output.heating_led, now) ? 1u : 0u,
        pattern_is_on(sm_output.fault_led, now) ? 1u : 0u,
        pattern_is_on(sm_output.heartbeat_led, now) ? 1u : 0u,
        QspiFlash_IsAvailable() ? "OK" : "NO",
        storage_available ? "OK" : "NO",
        fault_name(sm_output.fault));
    if (length > 0 && (size_t)length < sizeof(line)) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)length, 100u);
    }
}

static bool uart_send_line(const char *line)
{
    size_t length = strlen(line);
    return (length <= UINT16_MAX) &&
           (HAL_UART_Transmit(&huart1, (uint8_t *)line,
                              (uint16_t)length, 1000u) == HAL_OK);
}

static void start_qspi_log_stream(void)
{
    if (!QspiFlash_IsAvailable()) {
        (void)uart_send_line("ERR,code=FLASH_UNAVAILABLE\r\n");
        return;
    }
    log_scan_active = true;
    log_scan_found_latest = false;
    log_scan_slot = 0u;
    log_scan_count = 0u;
    log_stream_max_sequence = 0u;
    log_stream_active = false;
}

static void process_qspi_log_scan(void)
{
    StorageLogRecord_t record;
    bool valid;
    char line[64];
    int length;
    uint32_t scanned = 0u;

    if (!log_scan_active) return;
    while ((log_scan_slot < STORAGE_LOG_CAPACITY) &&
           (scanned < APP_QSPI_SCAN_SLOTS_PER_TASK)) {
        if (Storage_ReadLogSlot(log_scan_slot++, &record, &valid) != STORAGE_OK) {
            log_scan_active = false;
            (void)uart_send_line("ERR,code=FLASH_READ\r\n");
            return;
        }
        ++scanned;
        if (valid) {
            ++log_scan_count;
            if (!log_scan_found_latest ||
                ((int32_t)(record.sequence - log_stream_max_sequence) > 0)) {
                log_stream_max_sequence = record.sequence;
                log_scan_found_latest = true;
            }
        }
    }
    (void)HAL_IWDG_Refresh(&hiwdg);
    if (log_scan_slot < STORAGE_LOG_CAPACITY) return;

    log_scan_active = false;
    length = snprintf(line, sizeof(line), "LOG_BEGIN,count=%lu\r\n",
                      (unsigned long)log_scan_count);
    if ((length < 0) || ((size_t)length >= sizeof(line)) || !uart_send_line(line)) {
        return;
    }
    log_stream_slot = 0u;
    log_stream_sent_count = 0u;
    log_stream_active = (log_scan_count != 0u);
    if (log_scan_count == 0u) {
        (void)uart_send_line("LOG_END,count=0\r\n");
    }
}

static void send_next_qspi_log_record(void)
{
    StorageLogRecord_t record;
    bool valid;
    char line[192];
    int length;
    uint32_t scanned = 0u;

    if (!log_stream_active) return;
    while ((log_stream_slot < STORAGE_LOG_CAPACITY) &&
           (scanned < APP_QSPI_SCAN_SLOTS_PER_TASK)) {
        uint32_t slot = log_stream_slot++;
        ++scanned;
        if (Storage_ReadLogSlot(slot, &record, &valid) != STORAGE_OK) {
            log_stream_active = false;
            (void)uart_send_line("ERR,code=FLASH_READ\r\n");
            return;
        }
        if (!valid || ((int32_t)(record.sequence - log_stream_max_sequence) > 0)) {
            continue;
        }
        length = snprintf(line, sizeof(line),
            "LOG,seq=%lu,tick_ms=%lu,event=%u,state=%u,fault=%u,temp_x10=%d,cup_mm=%u,strength=%u,flags=%u,heater=%u,pump=%u,crc=%04X\r\n",
            (unsigned long)record.sequence, (unsigned long)record.tick_ms,
            (unsigned)record.event_id, (unsigned)record.state,
            (unsigned)record.fault, (int)record.temperature_x10,
            (unsigned)record.cup_distance_mm, (unsigned)record.strength,
            (unsigned)record.flags, (unsigned)record.heater_percent,
            (unsigned)record.pump_active, (unsigned)record.crc16);
        if ((length < 0) || ((size_t)length >= sizeof(line)) || !uart_send_line(line)) {
            log_stream_active = false;
            (void)uart_send_line("ERR,code=UART_TX\r\n");
            return;
        }
        ++log_stream_sent_count;
        (void)HAL_IWDG_Refresh(&hiwdg);
        return;
    }

    if (log_stream_slot < STORAGE_LOG_CAPACITY) return;

    log_stream_active = false;
    length = snprintf(line, sizeof(line), "LOG_END,count=%lu\r\n",
                      (unsigned long)log_stream_sent_count);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        (void)uart_send_line(line);
    }
}

static void process_uart_command(void)
{
    if (uart_command_overflow) {
        (void)uart_send_line("ERR,code=COMMAND_TOO_LONG\r\n");
    } else if (strcmp(uart_command, "CMD,READ_LOGS") == 0) {
        if (log_stream_active || log_scan_active) {
            (void)uart_send_line("ERR,code=BUSY\r\n");
        } else {
            (void)uart_send_line("LOG_SCAN\r\n");
            start_qspi_log_stream();
        }
    } else if (uart_command_length != 0u) {
        (void)uart_send_line("ERR,code=UNKNOWN_COMMAND\r\n");
    }
    uart_command_length = 0u;
    uart_command_overflow = false;
    uart_command[0] = '\0';
}

static void poll_uart_commands(void)
{
    uint8_t byte;
    while (HAL_UART_Receive(&huart1, &byte, 1u, 0u) == HAL_OK) {
        if (byte == '\r') continue;
        if (byte == '\n') {
            uart_command[uart_command_length] = '\0';
            process_uart_command();
        } else if (!uart_command_overflow) {
            if (uart_command_length < (sizeof(uart_command) - 1u)) {
                uart_command[uart_command_length++] = (char)byte;
            } else {
                uart_command_overflow = true;
            }
        }
    }
}

static void apply_outputs(uint32_t now)
{
    write_pattern(D5_LED_READY_GPIO_Port, D5_LED_READY_Pin, sm_output.ready_led, now);
    write_pattern(D6_LED_HEATING_GPIO_Port, D6_LED_HEATING_Pin, sm_output.heating_led, now);
    write_pattern(D7_LED_FAULT_GPIO_Port, D7_LED_FAULT_Pin, sm_output.fault_led, now);
    write_pattern(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin,
                  sm_output.heartbeat_led, now);
}

void CoffeeApp_Init(void)
{
    uint16_t adc_value = 0u;
    uint32_t now = HAL_GetTick();
    TofCalibrationProfile_t calibration_profile;

    coffee_sm_init(&coffee_sm);
    input_init(&power_button, BTN_POWER_GPIO_Port, BTN_POWER_Pin, now);
    input_init(&start_button, D2_BTN_START_CANCEL_GPIO_Port,
               D2_BTN_START_CANCEL_Pin, now);
    input_init(&strength_switch, D4_SW_STRENGTH_GPIO_Port,
               D4_SW_STRENGTH_Pin, now);
    adc_valid = HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) == HAL_OK;
    if (adc_valid && read_adc(&adc_value)) filtered_adc = adc_value;
    else adc_valid = false;
    tof_initialized = tof_init();
    tof_error_count = tof_initialized ? 0u : 3u;
    storage_init_in_progress = (Storage_BeginInit() == STORAGE_BUSY);
    storage_available = false;
    storage_boot_log_pending = true;
    if (QspiFlash_IsAvailable() && TofCalibrationStore_Load(&calibration_profile)) {
        cup_present_threshold_mm = calibration_profile.cup_present_mm;
        cup_absent_threshold_mm = calibration_profile.cup_absent_mm;
    }
    last_task_tick = HAL_GetTick();
    last_tof_tick = last_task_tick - APP_TOF_PERIOD_MS;
    last_tof_retry_tick = last_task_tick;
    last_report_tick = last_task_tick - APP_REPORT_PERIOD_MS;
    last_storage_retry_tick = last_task_tick;

    memset(&sm_input, 0, sizeof(sm_input));
    sm_input.water_temp_valid = adc_valid;
    sm_input.water_temp_c = APP_TEMP_MIN_C +
        ((float)filtered_adc * (APP_TEMP_MAX_C - APP_TEMP_MIN_C) / 4095.0f);
    sm_input.selected_strength = COFFEE_STRENGTH_NORMAL;
    coffee_sm_step(&coffee_sm, &sm_input, NULL, &sm_output);
}

void CoffeeApp_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint16_t adc_value;
    uint32_t elapsed = now - last_task_tick;
    float temperature_c;

    last_task_tick = now;
    if (read_adc(&adc_value)) {
        filtered_adc = (uint16_t)(((uint32_t)filtered_adc * 3u + adc_value) / 4u);
        adc_valid = true;
    } else {
        adc_valid = false;
    }
    temperature_c = APP_TEMP_MIN_C +
        ((float)filtered_adc * (APP_TEMP_MAX_C - APP_TEMP_MIN_C) / 4095.0f);

    input_update(&power_button, now, true);
    input_update(&start_button, now, true);
    input_update(&strength_switch, now, false);
    read_tof(now);

    memset(&sm_input, 0, sizeof(sm_input));
    sm_input.delta_ms = elapsed;
    sm_input.water_temp_c = temperature_c;
    sm_input.water_temp_valid = adc_valid;
    sm_input.cup_distance_mm = cup_distance_mm;
    sm_input.cup_sensor_valid = tof_valid;
    sm_input.cup_present = cup_present;
    sm_input.cup_sensor_error_count = tof_error_count;
    sm_input.selected_strength = selected_strength();
    sm_input.power_pressed = power_button.pressed_event;
    sm_input.start_cancel_pressed = start_button.pressed_event;
    sm_input.tank_empty = false; /* No tank-level sensor is in this hardware design. */

    coffee_sm_step(&coffee_sm, &sm_input, NULL, &sm_output);
    if (storage_available && (sm_output.event != COFFEE_EVENT_NONE)) {
        if (Storage_LogEvent((uint16_t)sm_output.event, now,
                             &sm_output, &sm_input) != STORAGE_OK) {
            storage_available = false;
        }
    }
    apply_outputs(now);
    send_status(now, temperature_c);
    poll_uart_commands();
    process_qspi_log_scan();
    send_next_qspi_log_record();
    if (!log_stream_active && !log_scan_active) {
        if (storage_init_in_progress) {
            StorageStatus_t init_status = Storage_InitStep(APP_QSPI_SCAN_SLOTS_PER_TASK);
            if (init_status == STORAGE_OK) {
                storage_init_in_progress = false;
                storage_available = true;
                if (storage_boot_log_pending) {
                    storage_boot_log_pending = false;
                    if (Storage_LogEvent(STORAGE_EVENT_BOOT, now,
                                         &sm_output, &sm_input) != STORAGE_OK) {
                        storage_available = false;
                        last_storage_retry_tick = now;
                    }
                }
            } else if (init_status != STORAGE_BUSY) {
                storage_init_in_progress = false;
                last_storage_retry_tick = now;
            }
        } else if (!storage_available &&
            (uint32_t)(now - last_storage_retry_tick) >= APP_STORAGE_RETRY_PERIOD_MS) {
            last_storage_retry_tick = now;
            storage_init_in_progress = (Storage_BeginInit() == STORAGE_BUSY);
        }
    }
}
