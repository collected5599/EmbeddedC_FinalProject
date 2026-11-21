#ifndef COFFEE_SM_H
#define COFFEE_SM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COFFEE_STATE_STANDBY = 0,
    COFFEE_STATE_IDLE,
    COFFEE_STATE_HEATING,
    COFFEE_STATE_BREWING,
    COFFEE_STATE_COMPLETE,
    COFFEE_STATE_FAULT
} CoffeeState_t;

typedef enum {
    COFFEE_FAULT_NONE = 0,
    COFFEE_FAULT_NO_CUP,
    COFFEE_FAULT_OVERTEMP,
    COFFEE_FAULT_HEAT_TIMEOUT,
    COFFEE_FAULT_SENSOR_COMM,
    COFFEE_FAULT_TANK_EMPTY
} CoffeeFault_t;

typedef enum {
    COFFEE_STRENGTH_NORMAL = 0,
    COFFEE_STRENGTH_STRONG
} CoffeeStrength_t;

typedef enum {
    COFFEE_LED_OFF = 0,
    COFFEE_LED_ON,
    COFFEE_LED_BLINK_1HZ,
    COFFEE_LED_BLINK_4HZ
} CoffeeLedPattern_t;

typedef enum {
    COFFEE_EVENT_NONE = 0,
    COFFEE_EVENT_STATE_CHANGED = (1u << 0),
    COFFEE_EVENT_USER_START = (1u << 1),
    COFFEE_EVENT_USER_START_IGNORED = (1u << 2),
    COFFEE_EVENT_USER_CANCEL = (1u << 3),
    COFFEE_EVENT_BREW_COMPLETE = (1u << 4),
    COFFEE_EVENT_FAULT_SET = (1u << 5),
    COFFEE_EVENT_FAULT_CLEARED = (1u << 6)
} CoffeeEvent_t;

typedef struct {
    float target_temp_normal_c;
    float target_temp_strong_c;
    float overtemp_limit_c;
    float overtemp_recovery_c;
    uint16_t brew_time_normal_s;
    uint16_t brew_time_strong_s;
    uint16_t preinfusion_normal_s;
    uint16_t preinfusion_strong_s;
    uint32_t heating_timeout_ms;
    uint32_t cup_absent_timeout_ms;
    uint32_t complete_timeout_ms;
    uint32_t bloom_pause_ms;
} CoffeeConfig_t;

typedef struct {
    float water_temp_c;
    uint16_t cup_distance_mm;
    uint8_t cup_sensor_error_count;
    uint32_t delta_ms;
    CoffeeStrength_t selected_strength;
    bool water_temp_valid;
    bool cup_sensor_valid;
    bool cup_present;
    bool tank_empty;
    bool power_pressed;
    bool start_cancel_pressed;
} CoffeeSmInput_t;

typedef struct {
    CoffeeState_t state;
    CoffeeFault_t fault;
    CoffeeStrength_t strength;
    uint32_t state_elapsed_ms;
    uint32_t cup_absent_ms;
    uint32_t brew_phase_elapsed_ms;
    uint8_t brew_phase;
} CoffeeSm_t;

typedef struct {
    CoffeeState_t state;
    CoffeeFault_t fault;
    CoffeeStrength_t strength;
    float target_temp_c;
    uint8_t heater_power_percent;
    bool pump_active;
    CoffeeLedPattern_t ready_led;
    CoffeeLedPattern_t heating_led;
    CoffeeLedPattern_t fault_led;
    CoffeeLedPattern_t heartbeat_led;
    CoffeeEvent_t event;
    bool clear_fault;
} CoffeeSmOutput_t;

void coffee_sm_init(CoffeeSm_t *machine);
void coffee_sm_step(CoffeeSm_t *machine,
                    const CoffeeSmInput_t *input,
                    const CoffeeConfig_t *config,
                    CoffeeSmOutput_t *output);

#endif
