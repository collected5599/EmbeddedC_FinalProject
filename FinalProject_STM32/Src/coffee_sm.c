#include "coffee_sm.h"

#include <string.h>

enum {
    BREW_PHASE_PREINFUSION = 0,
    BREW_PHASE_BLOOM_PAUSE,
    BREW_PHASE_EXTRACTION
};

static const CoffeeConfig_t default_config = {
    .target_temp_normal_c = 92.0f,
    .target_temp_strong_c = 95.0f,
    .overtemp_limit_c = 102.0f,
    .overtemp_recovery_c = 80.0f,
    .brew_time_normal_s = 20u,
    .brew_time_strong_s = 32u,
    .preinfusion_normal_s = 2u,
    .preinfusion_strong_s = 5u,
    .heating_timeout_ms = 180000u,
    .cup_absent_timeout_ms = 200u,
    .complete_timeout_ms = 5000u,
    .bloom_pause_ms = 2000u
};

static uint32_t add_saturating(uint32_t a, uint32_t b)
{
    return (UINT32_MAX - a < b) ? UINT32_MAX : a + b;
}

static const CoffeeConfig_t *get_config(const CoffeeConfig_t *config)
{
    return (config != NULL) ? config : &default_config;
}

static void change_state(CoffeeSm_t *machine,
                         CoffeeState_t state,
                         CoffeeSmOutput_t *output)
{
    if (machine->state != state) {
        machine->state = state;
        machine->state_elapsed_ms = 0u;
        machine->cup_absent_ms = 0u;
        output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_STATE_CHANGED);
    }
}

static CoffeeFault_t find_fault(CoffeeSm_t *machine,
                                const CoffeeSmInput_t *input,
                                const CoffeeConfig_t *config)
{
    const bool active_cycle = (machine->state == COFFEE_STATE_HEATING) ||
                              (machine->state == COFFEE_STATE_BREWING);

    if (machine->state == COFFEE_STATE_STANDBY ||
        machine->state == COFFEE_STATE_FAULT) {
        return COFFEE_FAULT_NONE;
    }

    if (!input->water_temp_valid ||
        (input->water_temp_c > config->overtemp_limit_c)) {
        return input->water_temp_valid ? COFFEE_FAULT_OVERTEMP :
                                         COFFEE_FAULT_SENSOR_COMM;
    }
    if (input->tank_empty) {
        return COFFEE_FAULT_TANK_EMPTY;
    }
    if (!input->cup_sensor_valid && input->cup_sensor_error_count >= 3u) {
        return COFFEE_FAULT_SENSOR_COMM;
    }

    if (active_cycle) {
        if (input->cup_sensor_valid && !input->cup_present) {
            machine->cup_absent_ms = add_saturating(machine->cup_absent_ms,
                                                     input->delta_ms);
            if (machine->cup_absent_ms >= config->cup_absent_timeout_ms) {
                return COFFEE_FAULT_NO_CUP;
            }
        } else {
            machine->cup_absent_ms = 0u;
        }
    }

    if ((machine->state == COFFEE_STATE_HEATING) &&
        (machine->state_elapsed_ms >= config->heating_timeout_ms)) {
        return COFFEE_FAULT_HEAT_TIMEOUT;
    }

    return COFFEE_FAULT_NONE;
}

static bool fault_is_clearable(CoffeeFault_t fault,
                               const CoffeeSmInput_t *input,
                               const CoffeeConfig_t *config)
{
    switch (fault) {
    case COFFEE_FAULT_NONE:
    case COFFEE_FAULT_HEAT_TIMEOUT:
        return true;
    case COFFEE_FAULT_NO_CUP:
        return input->cup_sensor_valid && input->cup_present;
    case COFFEE_FAULT_OVERTEMP:
        return input->water_temp_valid &&
               (input->water_temp_c < config->overtemp_recovery_c);
    case COFFEE_FAULT_SENSOR_COMM:
        return input->cup_sensor_valid && input->water_temp_valid &&
               (input->cup_sensor_error_count == 0u);
    case COFFEE_FAULT_TANK_EMPTY:
        return !input->tank_empty;
    default:
        return false;
    }
}

static uint32_t brew_phase_duration_ms(const CoffeeSm_t *machine,
                                      const CoffeeConfig_t *config)
{
    const uint32_t preinfusion_s = (machine->strength == COFFEE_STRENGTH_STRONG) ?
        config->preinfusion_strong_s : config->preinfusion_normal_s;
    const uint32_t brew_s = (machine->strength == COFFEE_STRENGTH_STRONG) ?
        config->brew_time_strong_s : config->brew_time_normal_s;

    switch (machine->brew_phase) {
    case BREW_PHASE_PREINFUSION:
        return preinfusion_s * 1000u;
    case BREW_PHASE_BLOOM_PAUSE:
        return config->bloom_pause_ms;
    case BREW_PHASE_EXTRACTION:
        return (brew_s > preinfusion_s) ? (brew_s - preinfusion_s) * 1000u : 0u;
    default:
        return 0u;
    }
}

static void advance_brew(CoffeeSm_t *machine,
                         const CoffeeConfig_t *config,
                         CoffeeSmOutput_t *output)
{
    (void)output;
    while (machine->brew_phase < BREW_PHASE_EXTRACTION) {
        const uint32_t duration = brew_phase_duration_ms(machine, config);
        if (machine->brew_phase_elapsed_ms < duration) {
            break;
        }
        machine->brew_phase_elapsed_ms -= duration;
        ++machine->brew_phase;
    }

    if ((machine->brew_phase == BREW_PHASE_EXTRACTION) &&
        (machine->brew_phase_elapsed_ms >= brew_phase_duration_ms(machine, config))) {
        change_state(machine, COFFEE_STATE_COMPLETE, output);
        output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_BREW_COMPLETE);
    }
}

void coffee_sm_init(CoffeeSm_t *machine)
{
    if (machine == NULL) {
        return;
    }
    memset(machine, 0, sizeof(*machine));
    machine->state = COFFEE_STATE_STANDBY;
    machine->fault = COFFEE_FAULT_NONE;
    machine->strength = COFFEE_STRENGTH_NORMAL;
}

void coffee_sm_step(CoffeeSm_t *machine,
                    const CoffeeSmInput_t *input,
                    const CoffeeConfig_t *config_arg,
                    CoffeeSmOutput_t *output)
{
    const CoffeeConfig_t *config = get_config(config_arg);
    CoffeeFault_t detected_fault;

    if ((machine == NULL) || (input == NULL) || (output == NULL)) {
        return;
    }

    memset(output, 0, sizeof(*output));
    machine->state_elapsed_ms = add_saturating(machine->state_elapsed_ms,
                                                input->delta_ms);
    output->event = COFFEE_EVENT_NONE;

    detected_fault = find_fault(machine, input, config);
    if ((detected_fault != COFFEE_FAULT_NONE) &&
        (machine->state != COFFEE_STATE_FAULT)) {
        machine->fault = detected_fault;
        change_state(machine, COFFEE_STATE_FAULT, output);
        output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_FAULT_SET);
    }

    switch (machine->state) {
    case COFFEE_STATE_STANDBY:
        if (input->power_pressed) {
            machine->fault = COFFEE_FAULT_NONE;
            change_state(machine, COFFEE_STATE_IDLE, output);
        }
        break;

    case COFFEE_STATE_IDLE:
        if (input->power_pressed) {
            change_state(machine, COFFEE_STATE_STANDBY, output);
        } else if (input->start_cancel_pressed) {
            if (!input->cup_sensor_valid || !input->cup_present) {
                output->event = COFFEE_EVENT_USER_START_IGNORED;
            } else {
                machine->strength = input->selected_strength;
                output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_USER_START);
                if (input->water_temp_c >=
                    ((machine->strength == COFFEE_STRENGTH_STRONG) ?
                     config->target_temp_strong_c : config->target_temp_normal_c)) {
                    machine->brew_phase = BREW_PHASE_PREINFUSION;
                    machine->brew_phase_elapsed_ms = 0u;
                    change_state(machine, COFFEE_STATE_BREWING, output);
                } else {
                    change_state(machine, COFFEE_STATE_HEATING, output);
                }
            }
        }
        break;

    case COFFEE_STATE_HEATING:
        if (input->start_cancel_pressed) {
            machine->fault = COFFEE_FAULT_NONE;
            change_state(machine, COFFEE_STATE_IDLE, output);
            output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_USER_CANCEL);
        } else if (input->water_temp_c >=
                   ((machine->strength == COFFEE_STRENGTH_STRONG) ?
                    config->target_temp_strong_c : config->target_temp_normal_c)) {
            machine->brew_phase = BREW_PHASE_PREINFUSION;
            machine->brew_phase_elapsed_ms = 0u;
            change_state(machine, COFFEE_STATE_BREWING, output);
        }
        break;

    case COFFEE_STATE_BREWING:
        if (input->start_cancel_pressed) {
            machine->fault = COFFEE_FAULT_NONE;
            change_state(machine, COFFEE_STATE_IDLE, output);
            output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_USER_CANCEL);
        } else {
            machine->brew_phase_elapsed_ms = add_saturating(
                machine->brew_phase_elapsed_ms, input->delta_ms);
            advance_brew(machine, config, output);
        }
        break;

    case COFFEE_STATE_COMPLETE:
        if (!input->cup_sensor_valid || !input->cup_present ||
            (machine->state_elapsed_ms >= config->complete_timeout_ms)) {
            change_state(machine, COFFEE_STATE_IDLE, output);
        }
        break;

    case COFFEE_STATE_FAULT:
        if (input->power_pressed) {
            machine->fault = COFFEE_FAULT_NONE;
            change_state(machine, COFFEE_STATE_STANDBY, output);
        } else if (input->start_cancel_pressed &&
                   fault_is_clearable(machine->fault, input, config)) {
            machine->fault = COFFEE_FAULT_NONE;
            output->clear_fault = true;
            output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_FAULT_CLEARED);
            change_state(machine, COFFEE_STATE_IDLE, output);
            output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_FAULT_CLEARED);
        }
        break;

    default:
        machine->fault = COFFEE_FAULT_SENSOR_COMM;
        change_state(machine, COFFEE_STATE_FAULT, output);
        output->event = (CoffeeEvent_t)(output->event | COFFEE_EVENT_FAULT_SET);
        break;
    }

    output->state = machine->state;
    output->fault = machine->fault;
    output->strength = machine->strength;
    output->heartbeat_led = COFFEE_LED_BLINK_1HZ;
    if (machine->state == COFFEE_STATE_IDLE) {
        const float target = (input->selected_strength == COFFEE_STRENGTH_STRONG) ?
            config->target_temp_strong_c : config->target_temp_normal_c;
        if (input->cup_sensor_valid && input->cup_present &&
            input->water_temp_valid && (input->water_temp_c >= target)) {
            output->ready_led = COFFEE_LED_ON;
        }
    } else if (machine->state == COFFEE_STATE_HEATING) {
        output->target_temp_c = (machine->strength == COFFEE_STRENGTH_STRONG) ?
            config->target_temp_strong_c : config->target_temp_normal_c;
        output->heater_power_percent = 100u;
        output->heating_led = COFFEE_LED_BLINK_1HZ;
    } else if (machine->state == COFFEE_STATE_BREWING) {
        output->target_temp_c = (machine->strength == COFFEE_STRENGTH_STRONG) ?
            config->target_temp_strong_c : config->target_temp_normal_c;
        output->pump_active = (machine->brew_phase != BREW_PHASE_BLOOM_PAUSE);
        output->heating_led = COFFEE_LED_ON;
    } else if (machine->state == COFFEE_STATE_COMPLETE) {
        output->ready_led = COFFEE_LED_BLINK_1HZ;
        output->heating_led = COFFEE_LED_BLINK_1HZ;
    } else if (machine->state == COFFEE_STATE_FAULT) {
        output->fault_led = COFFEE_LED_BLINK_4HZ;
    }
}
