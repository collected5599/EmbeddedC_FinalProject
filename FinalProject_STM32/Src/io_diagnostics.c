#include "io_diagnostics.h"

#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c2;
extern UART_HandleTypeDef huart1;

enum {
    DIAG_LED_READY = 0,
    DIAG_LED_HEATING,
    DIAG_LED_FAULT,
    DIAG_LED_HEARTBEAT,
    DIAG_LED_ALL_OFF,
    DIAG_LED_COUNT
};

#define DIAG_REPORT_PERIOD_MS 500u
#define DIAG_LED_STEP_MS      500u
#define DIAG_TOF_PERIOD_MS    1000u
#define DIAG_TOF_ADDRESS      (0x29u << 1)

static uint32_t led_tick;
static uint32_t report_tick;
static uint32_t tof_tick;
static uint8_t led_step;
static uint8_t led_active = DIAG_LED_ALL_OFF;
static bool adc_calibrated;
static HAL_StatusTypeDef tof_status = HAL_BUSY;

static void set_test_leds(uint8_t active_led)
{
    HAL_GPIO_WritePin(D5_LED_READY_GPIO_Port, D5_LED_READY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(D6_LED_HEATING_GPIO_Port, D6_LED_HEATING_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(D7_LED_FAULT_GPIO_Port, D7_LED_FAULT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin, GPIO_PIN_RESET);

    switch (active_led) {
    case DIAG_LED_READY:
        HAL_GPIO_WritePin(D5_LED_READY_GPIO_Port, D5_LED_READY_Pin, GPIO_PIN_SET);
        break;
    case DIAG_LED_HEATING:
        HAL_GPIO_WritePin(D6_LED_HEATING_GPIO_Port, D6_LED_HEATING_Pin, GPIO_PIN_SET);
        break;
    case DIAG_LED_FAULT:
        HAL_GPIO_WritePin(D7_LED_FAULT_GPIO_Port, D7_LED_FAULT_Pin, GPIO_PIN_SET);
        break;
    case DIAG_LED_HEARTBEAT:
        HAL_GPIO_WritePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin, GPIO_PIN_SET);
        break;
    case DIAG_LED_ALL_OFF:
    default:
        break;
    }
}

static const char *pin_level(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

static bool read_pot_adc(uint32_t *value)
{
    if (!adc_calibrated || (HAL_ADC_Start(&hadc1) != HAL_OK)) {
        return false;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return false;
    }

    *value = HAL_ADC_GetValue(&hadc1);
    return (HAL_ADC_Stop(&hadc1) == HAL_OK);
}

static const char *tof_status_text(void)
{
    if (tof_status == HAL_BUSY) {
        return "WAIT";
    }
    return (tof_status == HAL_OK) ? "ACK" : "NO_ACK";
}

static const char *active_led_text(void)
{
    switch (led_active) {
    case DIAG_LED_READY:
        return "READY";
    case DIAG_LED_HEATING:
        return "HEATING";
    case DIAG_LED_FAULT:
        return "FAULT";
    case DIAG_LED_HEARTBEAT:
        return "HEARTBEAT";
    case DIAG_LED_ALL_OFF:
    default:
        return "OFF";
    }
}

static void send_report(void)
{
    char line[160];
    uint32_t adc_value = 0u;
    int length;

    if (read_pot_adc(&adc_value)) {
        length = snprintf(line, sizeof(line),
                          "DIAG D2=%s D4=%s PC13=%s PC5_ADC=%lu/4095 TOF=%s LED=%s\r\n",
                          pin_level(D2_BTN_START_CANCEL_GPIO_Port,
                                    D2_BTN_START_CANCEL_Pin),
                          pin_level(D4_SW_STRENGTH_GPIO_Port,
                                    D4_SW_STRENGTH_Pin),
                          pin_level(BTN_POWER_GPIO_Port, BTN_POWER_Pin),
                          (unsigned long)adc_value,
                          tof_status_text(),
                          active_led_text());
    } else {
        length = snprintf(line, sizeof(line),
                          "DIAG D2=%s D4=%s PC13=%s PC5_ADC=ERR TOF=%s LED=%s\r\n",
                          pin_level(D2_BTN_START_CANCEL_GPIO_Port,
                                    D2_BTN_START_CANCEL_Pin),
                          pin_level(D4_SW_STRENGTH_GPIO_Port,
                                    D4_SW_STRENGTH_Pin),
                          pin_level(BTN_POWER_GPIO_Port, BTN_POWER_Pin),
                          tof_status_text(),
                          active_led_text());
    }

    if ((length > 0) && ((size_t)length < sizeof(line))) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                               (uint16_t)length, 100u);
    }
}

void IoDiagnostics_Init(void)
{
    const HAL_StatusTypeDef calibration_status =
        HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    adc_calibrated = (calibration_status == HAL_OK);
    led_step = DIAG_LED_READY;
    led_active = DIAG_LED_ALL_OFF;
    led_tick = HAL_GetTick();
    report_tick = led_tick;
    tof_tick = led_tick;
    set_test_leds(DIAG_LED_ALL_OFF);
}

void IoDiagnostics_Task(void)
{
    const uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - led_tick) >= DIAG_LED_STEP_MS) {
        led_tick = now;
        led_active = led_step;
        set_test_leds(led_active);
        led_step = (uint8_t)((led_step + 1u) % DIAG_LED_COUNT);
    }

    if ((uint32_t)(now - tof_tick) >= DIAG_TOF_PERIOD_MS) {
        tof_tick = now;
        tof_status = HAL_I2C_IsDeviceReady(&hi2c2, DIAG_TOF_ADDRESS,
                                           2u, 15u);
    }

    if ((uint32_t)(now - report_tick) >= DIAG_REPORT_PERIOD_MS) {
        report_tick = now;
        send_report();
    }
}
