# STM32 IoT Coffee Maker — Firmware Design Specification

**Target Platform**: STMicroelectronics B-L475E-IOT01A1 (STM32L475VGT6)  
**Toolchain**: STM32CubeIDE 2.2.0 / GCC for ARM Embedded Processors  
**Firmware Paradigm**: Bare-metal Superloop with Non-Blocking Delta-Tick Cooperative Scheduling  
**Hardware Abstraction**: STM32Cube HAL (V1.18.2)  
**Revision**: 1.2  
**Date**: 2026-11-20  

---

## 1. Firmware Architecture & Directory Layout

To maintain strict modularity, testability, and portability, all application code is decoupled from CubeMX-generated boilerplate:

```text
<project_root>/
├── Core/
│   ├── Inc/                 # CubeMX generated HAL headers (main.h, stm32l4xx_it.h)
│   └── Src/                 # CubeMX generated HAL entry & ISRs (main.c, stm32l4xx_it.c)
├── Drivers/                 # STM32L4xx_HAL_Driver & CMSIS
├── App/                     # Modular Coffee Maker Application Firmware
│   ├── Inc/
│   │   ├── app_config.h     # System tunables (periods, thresholds, timeouts, mappings)
│   │   ├── app_main.h       # Application entry point and scheduler declarations
│   │   ├── button_manager.h # Non-blocking debounce and edge detection
│   │   ├── led_manager.h    # Non-blocking LED pattern generator
│   │   ├── temp_sensor.h    # ADC1 potentiometer driver with moving average
│   │   ├── cup_sensor.h     # VL53L0X ToF distance driver via I2C2
│   │   ├── qspi_flash.h     # Macronix MX25R6435F low-level Quad-SPI driver
│   │   ├── storage_manager.h# Ping-pong config persistence & circular logging
│   │   ├── coffee_sm.h      # Coffee State Machine engine
│   │   ├── fault_manager.h  # Safety supervisor & interlock enforcement
│   │   └── uart_protocol.h  # Binary packet framing, telemetry & commands
│   └── Src/
│       ├── app_main.c
│       ├── button_manager.c
│       ├── led_manager.c
│       ├── temp_sensor.c
│       ├── cup_sensor.c
│       ├── qspi_flash.c
│       ├── storage_manager.c
│       ├── coffee_sm.c
│       ├── fault_manager.c
│       └── uart_protocol.c
└── Documentation/           # System, Hardware, and Architecture Specifications
```

---

## 2. Superloop Cooperative Scheduler & Non-Blocking Rules

### 2.1 Scheduling Principles
1. **Zero Blocking**: The function `HAL_Delay()` is strictly forbidden in all application code. All driver operations (QSPI flash erase/write, UART transmission, I2C reads) must be non-blocking or bounded by microsecond timeouts ($\le 5\text{ ms}$).
2. **Delta-Tick Arithmetic**: All timing intervals are evaluated using unsigned subtraction to guarantee immunity to 32-bit tick overflow:
   $$\Delta t = (\text{HAL\_GetTick}() - \text{last\_execution\_tick}) \ge \text{PERIOD\_MS}$$
3. **Loop Execution Order**:
   Every pass of the superloop processes tasks in deterministic order:
   * **Stage 1 — Inputs**: Sample switches and buttons (`button_manager_process`)
   * **Stage 2 — Sensors**: Process ADC temperature and I2C distance readings (`temp_sensor_process`, `cup_sensor_process`)
   * **Stage 3 — Safety Supervisor**: Evaluate safety invariants and interlocks (`fault_manager_process`)
   * **Stage 4 — State Machine**: Update state transitions and timers (`coffee_sm_process`)
   * **Stage 5 — Outputs**: Update LED patterns and onboard heartbeat (`led_manager_process`)
   * **Stage 6 — Storage Queue**: Service non-blocking flash erase/write jobs (`storage_manager_process`)
   * **Stage 7 — Comms**: Process incoming UART packets and transmit 10 Hz telemetry (`uart_protocol_process`)
4. **Execution Cadence Matrix**:

| Task / Module | Interval | Method | Description |
| :--- | :---: | :---: | :--- |
| **Button Debounce** | 10 ms | `button_manager_process()` | Samples GPIOs, runs 30 ms debouncing filter, fires event flags. |
| **LED Blink Update** | 20 ms | `led_manager_process()` | Updates LED states (Blink, Solid, Pulse) and 1 Hz onboard Heartbeat. |
| **Temperature Sample** | 50 ms | `temp_sensor_process()` | Triggers non-blocking ADC conversion, updates exponential moving average. |
| **Cup Distance Sample**| 100 ms | `cup_sensor_process()` | Reads VL53L0X distance register; qualifies presence with hysteresis. |
| **Fault Check** | 20 ms | `fault_manager_process()` | Verifies all safety invariants (over-temp, cup absent mid-brew). |
| **State Machine Tick** | 50 ms | `coffee_sm_process()` | Evaluates transitions, timers, and actuator requests. |
| **UART Telemetry** | 100 ms | `uart_protocol_process()` | Emits 10 Hz telemetry packet to Python twin; parses incoming commands. |
| **Flash Job Service** | Loop pass | `storage_manager_process()` | Polls WIP bit on in-flight flash sector erases and page writes. |

---

## 3. Module Specifications & API Definitions

### 3.1 Button Manager (`button_manager.h/.c`)
* **Role**: Debounces noisy mechanical switches and generates discrete events: `BTN_EVENT_PRESSED`, `BTN_EVENT_RELEASED`, and `BTN_EVENT_HELD`.
* **Hardware Mappings**:
  * Onboard Power Button: `PC13` (User Button B2, `BUTTON_EXTI13`, Active-Low; Note: B1 is NRST Reset)
  * External Start/Cancel: `PD14` (Active-Low, internal pull-up)
  * External Lever Switch: `PA3` (Active-Low, internal pull-up)
* **Strength Latching Policy**: Coffee strength (Normal vs Strong) is latched at the moment the Start button is pressed. Lever toggles during active brewing are ignored until the cycle completes.

```c
typedef enum {
    BTN_POWER = 0,
    BTN_START_CANCEL,
    SW_STRENGTH,
    BTN_COUNT
} ButtonId_t;

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_PRESSED,
    BTN_EVENT_RELEASED,
    BTN_EVENT_HELD
} BtnEvent_t;

typedef enum {
    BTN_STATE_RELEASED = 0,
    BTN_STATE_PRESSED
} ButtonState_t;

typedef enum {
    SW_MODE_NORMAL = 0,  // Switch open (PA3 = 1)
    SW_MODE_STRONG  = 1   // Switch closed (PA3 = 0)
} CoffeeStrength_t;

void button_manager_init(void);
void button_manager_process(void);
BtnEvent_t button_manager_get_event(ButtonId_t btn);
bool button_manager_was_pressed(ButtonId_t btn);
bool button_manager_was_held(ButtonId_t btn, uint32_t hold_time_ms);
CoffeeStrength_t button_manager_get_strength(void);
```

---

### 3.2 LED Manager (`led_manager.h/.c`)
* **Role**: Governs all external LEDs (`PB4` Ready, `PB1` Heating, `PA4` Fault) and the onboard heartbeat LED (`PB14`).
* **Patterns Supported**: `LED_OFF`, `LED_ON`, `LED_BLINK_SLOW` (1 Hz, 50% duty), `LED_BLINK_FAST` (4 Hz, 50% duty), `LED_PULSE_ONCE`.

```c
typedef enum {
    LED_READY = 0,     // PB4 (Green)
    LED_HEATING,       // PB1 (Yellow)
    LED_FAULT,         // PA4 (Red)
    LED_HEARTBEAT,     // PB14 (Onboard Green LED2)
    LED_COUNT
} LedId_t;

typedef enum {
    LED_PATTERN_OFF = 0,
    LED_PATTERN_ON,
    LED_PATTERN_BLINK_1HZ,
    LED_PATTERN_BLINK_4HZ,
    LED_PATTERN_FLASH_CONFIRM
} LedPattern_t;

void led_manager_init(void);
void led_manager_process(void);
void led_manager_set_pattern(LedId_t led, LedPattern_t pattern);
```

---

### 3.3 Temperature Sensor Driver (`temp_sensor.h/.c`)
* **Role**: Reads the 10 kΩ potentiometer on `PC5` (ADC1_IN14) simulating boiler water temperature ($20^\circ\text{C}$ to $110^\circ\text{C}$).
* **Linear Calibration Equation & Knob Sensitivity**:
  $$T = 20.0^\circ\text{C} + \left(\frac{\text{ADC\_RAW}}{4095}\right) \times 90.0^\circ\text{C}$$
  Under this mapping ($20^\circ\text{C}$ to $110^\circ\text{C}$), the target brewing temperatures sit near the upper end of the knob:
  * $92.0^\circ\text{C}$ normal brew: $\frac{92 - 20}{90} = 80.0\%$ rotation
  * $95.0^\circ\text{C}$ strong brew: $\frac{95 - 20}{90} = 83.3\%$ rotation
  * $102.0^\circ\text{C}$ over-temp fault threshold: $\frac{102 - 20}{90} = 91.1\%$ rotation
  Each 1% of pot rotation equals $0.9^\circ\text{C}$.
* **Filtering**: Exponential Moving Average (EMA) with smoothing factor $\alpha = 0.25$:
  $$T_{\text{filtered}}[k] = 0.25 \cdot T_{\text{raw}}[k] + 0.75 \cdot T_{\text{filtered}}[k-1]$$
* **ADC Startup**: Invokes `HAL_ADCEx_Calibration_Start()` during initialization to calibrate offset errors.
* **Temperature Source Abstraction & Staleness Timeout (D1, D6)**: In `TEMP_SOURCE_POTENTIOMETER` mode (default), readings derive directly from ADC1_IN14. In `TEMP_SOURCE_TWIN_SIMULATION` mode, values are supplied via `MSG_INJECT_TEMP` from the Python digital twin. If no update arrives for $> 1000\text{ ms}$, the driver flags a staleness timeout and asserts `FAULT_SENSOR_COMM`.

```c
typedef enum {
    TEMP_SOURCE_POTENTIOMETER = 0,
    TEMP_SOURCE_TWIN_SIMULATION
} TempSource_t;

void temp_sensor_init(ADC_HandleTypeDef *hadc);
void temp_sensor_process(void);
float temp_sensor_get_temp_c(void);
uint16_t temp_sensor_get_raw_adc(void);
bool temp_sensor_is_overtemp(float limit_c);
void temp_sensor_set_source(TempSource_t src);
void temp_sensor_inject_temp(float temp_c);
bool temp_sensor_is_stale(void);
```

---

### 3.4 Cup Presence Sensor Driver (`cup_sensor.h/.c`)
* **Role**: Interfaces with the onboard ST VL53L0X Time-of-Flight sensor over I2C2 (`PB10` SCL, `PB11` SDA) with hardware enable on `PC6` (`XSHUT`).
* **Hysteresis Thresholding**:
  * Cup Present: distance $< T_{\text{thresh}}$ (default $80\text{ mm}$)
  * Cup Absent: distance $\ge T_{\text{thresh}} + 20\text{ mm}$ (default $100\text{ mm}$)
* **Error & Out-of-Range Handling**: If the sensor reports out-of-range (> 8000 mm) or reading phase failure, distance is marked invalid and cup state evaluates as absent. If 3 consecutive I2C reads fail, `FAULT_SENSOR_COMM` is asserted.

```c
void cup_sensor_init(I2C_HandleTypeDef *hi2c);
void cup_sensor_process(void);
uint16_t cup_sensor_get_distance_mm(void);
bool cup_sensor_is_cup_present(void);
bool cup_sensor_has_fault(void);
```

---

### 3.5 QSPI Flash Driver (`qspi_flash.h/.c`)
* **Role**: Non-blocking HAL driver for the 8 MB Macronix MX25R6435F NOR Flash on `PE10`–`PE15`.
* **Silicon Setup & Quad Enable (F2)**:
  * Reads the non-volatile Status Register (`0x05`) / Configuration Register (`0x15`). If the **`QE` (Quad Enable, bit 6)** is not set, executes `WREN` (`0x06`) and writes `QE = 1`.
  * Configures High-Performance mode (enabling up to 80 MHz clocking with 1 cycle chip-select hold).
* **Dummy Cycle Specification for Quad I/O (`0xEB`)**:
  * Command: 1-line (`0xEB`)
  * Address: 4-line (24 bits = 6 clocks)
  * Alternate Bytes / Mode: 4-line (8 bits = 2 clocks, sending `0xF0` continuous read mode disable)
  * Dummy Clocks: 4 dummy cycles (Total wait = $2 + 4 = 6$ clocks)
  * Data: 4-line Quad read
* **Low Power Modes for STANDBY (A6)**:
  * Deep Power-Down command (`0xB9`): drops standby current to $< 1\ \mu\text{A}$.
  * Release from Deep Power-Down (`0xAB`): requires $35\ \mu\text{s}$ wake-up latency before issuing next command.
* **Non-Blocking Operation & Read Deferral (F3, Item 4)**:
  * Erase and write functions return `QSPI_BUSY` while in progress.
  * `qspi_flash_read()` checks `qspi_flash_is_busy()`; if busy, it refuses the read (`QSPI_BUSY`) to prevent returning corrupt data while a sector erase is in flight.

```c
typedef enum {
    QSPI_OK = 0,
    QSPI_BUSY,
    QSPI_ERROR,
    QSPI_TIMEOUT
} QspiStatus_t;

QspiStatus_t qspi_flash_init(QSPI_HandleTypeDef *hqspi);
QspiStatus_t qspi_flash_enable_quad(void);
QspiStatus_t qspi_flash_read_id(uint32_t *jedec_id);
QspiStatus_t qspi_flash_erase_sector_start(uint32_t sector_address);
QspiStatus_t qspi_flash_write_page_start(uint32_t address, const uint8_t *data, uint16_t length);
bool qspi_flash_is_busy(void);
QspiStatus_t qspi_flash_read(uint32_t address, uint8_t *buffer, uint32_t length);
QspiStatus_t qspi_flash_deep_power_down(void);
QspiStatus_t qspi_flash_release_power_down(void);
```

---

### 3.6 Storage Manager (`storage_manager.h/.c`)
* **Role**: Provides power-loss safe configuration persistence (ping-pong sectors) and 32-byte circular event logging with boot-time head/tail recovery.

```c
#define CONFIG_MAGIC_WORD  0x434F4646  // 'COFF'
#define CONFIG_VERSION     0x00010001

typedef enum {
    EVT_BOOT = 0,
    EVT_STATE_CHANGE,
    EVT_USER_START,
    EVT_USER_CANCEL,
    EVT_USER_START_IGNORED,
    EVT_FAULT_SET,
    EVT_FAULT_CLEARED,
    EVT_CONFIG_SAVED,
    EVT_BREW_COMPLETE,
    EVT_SIM_INJECT
} EventId_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;              // Magic word validation
    uint32_t version;            // Schema version
    uint32_t revision;           // Incrementing write sequence number
    float    target_temp_normal; // Target temperature normal (default: 92.0 °C)
    float    target_temp_strong; // Target temperature strong (default: 95.0 °C)
    uint16_t brew_time_normal_s; // Brew duration normal (default: 20 s)
    uint16_t brew_time_strong_s; // Brew duration strong (default: 32 s)
    uint16_t preinfusion_time_s; // Pre-infusion duration (default: 2 s normal, 5 s strong)
    uint16_t cup_threshold_mm;   // Detection distance threshold (default: 80 mm)
    uint32_t crc32;              // Integrity checksum over all preceding fields
} MachineConfig_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;               // Monotonic write sequence number (head/tail recovery)
    uint32_t timestamp_ms;      // HAL_GetTick()
    uint8_t  event_id;          // EventId_t
    uint8_t  current_state;     // CoffeeState_t
    uint16_t water_temp_c_x10;  // Temp * 10
    uint16_t cup_dist_mm;       // Distance mm
    uint8_t  strength_mode;     // 0 = Normal, 1 = Strong
    uint8_t  fault_code;        // FaultCode_t
    uint32_t brew_count;        // Lifetime brew count at event time
    uint8_t  reserved[10];      // Padding to exactly 32 bytes
    uint16_t crc16;             // Payload CRC16 (CCITT-FALSE)
} LogEntry_t;                   // EXACTLY 32 bytes
_Static_assert(sizeof(LogEntry_t) == 32, "LogEntry_t must be exactly 32 bytes");

void storage_manager_init(void);
void storage_manager_process(void);
MachineConfig_t* storage_manager_get_config(void);
bool storage_manager_save_config(const MachineConfig_t *new_config);
bool storage_manager_log_event(EventId_t event_id, uint8_t state, uint8_t fault_code);
uint16_t storage_manager_get_log_count(void);
bool storage_manager_read_log(uint16_t index, LogEntry_t *entry);
bool storage_manager_clear_log(uint32_t confirm_token);
uint32_t storage_manager_get_lifetime_brews(void);
void storage_manager_increment_lifetime_brews(void);
```

---

### 3.7 Coffee State Machine (`coffee_sm.h/.c`)
* **Role**: Governs the machine lifecycle, operating modes, and state transitions.
* **Testability Architecture (F15)**: The core FSM logic is implemented as a pure function: it accepts an immutable input struct (including active configuration parameters) and populates an output struct with zero HAL dependencies. This permits 100% unit test coverage on the PC before running on hardware.
* **Button & Fault Semantics (Item 3)**:
  * External button `PD14` acts as a single input `btn_start_cancel_pressed`. The FSM determines context: in `IDLE` it requests Start, while in `HEATING` or `BREWING` it aborts to `IDLE`.
  * In `FAULT` state, if `btn_start_cancel_pressed` occurs, `coffee_sm_step()` checks if the active fault condition has physically resolved. If so, it sets `out->clear_fault = true` and transitions to `IDLE`; the superloop wrapper executes `fault_manager_clear_fault()`.

```c
typedef enum {
    STATE_STANDBY = 0,
    STATE_IDLE,
    STATE_HEATING,
    STATE_BREWING,
    STATE_COMPLETE,
    STATE_FAULT
} CoffeeState_t;

// Pure-logic Input Struct (HAL-free for PC unit testing)
typedef struct {
    float                  water_temp_c;
    bool                   cup_present;
    bool                   btn_power_pressed;
    bool                   btn_start_cancel_pressed;
    CoffeeStrength_t       strength_switch;
    FaultCode_t            active_fault;
    uint32_t               delta_ms;
    const MachineConfig_t *config;           // Active configuration parameters
} CoffeeSmInputs_t;

// Pure-logic Output Struct
typedef struct {
    CoffeeState_t state;
    float         target_temp_c;
    uint8_t       heater_power_pct;
    bool          pump_active;
    LedPattern_t  ready_led;
    LedPattern_t  heat_led;
    LedPattern_t  fault_led;
    EventId_t     emitted_event;
    bool          clear_fault;              // Instructs supervisor to clear latched fault
} CoffeeSmOutputs_t;

void coffee_sm_init(void);
void coffee_sm_step(const CoffeeSmInputs_t *in, CoffeeSmOutputs_t *out);
void coffee_sm_process(void); // Superloop wrapper mapping drivers to in/out
CoffeeState_t coffee_sm_get_state(void);
```

---

### 3.8 Fault Manager (`fault_manager.h/.c`)
* **Role**: Continuous safety supervisor evaluating hardware thresholds, safety interlocks, and sensor communication timeouts.

```c
typedef enum {
    FAULT_NONE = 0,
    FAULT_NO_CUP,          // 1: Cup absent for >= 200 ms during HEATING or BREWING
    FAULT_OVERTEMP,        // 2: Boiler temp > 102.0 °C
    FAULT_HEAT_TIMEOUT,    // 3: HEATING duration > 180 s without reaching target
    FAULT_SENSOR_COMM,     // 4: 3 consecutive VL53L0X I2C transaction failures
    FAULT_TANK_EMPTY       // 5: Injected by Python twin simulation
} FaultCode_t;

void fault_manager_init(void);
void fault_manager_process(void);
bool fault_manager_has_active_fault(void);
FaultCode_t fault_manager_get_code(void);
void fault_manager_clear_fault(void);
```

---

### 3.9 UART Protocol (`uart_protocol.h/.c`)
* **Role**: Packetizes 10 Hz telemetry for the Python twin, parses incoming configuration updates, and handles binary log streaming over USART1.
* **Reception & Framing (F12)**: Uses interrupt/DMA-driven circular ring buffer on RX. A byte-level state machine searches for `0xAA 0x55` SOF, checks 20 ms inter-byte timeout, validates length ($\le 64$ bytes), and verifies CCITT-FALSE CRC16 covering `MSG_ID` + `LENGTH` + `DATA`.

```c
#define UART_SOF_1  0xAA
#define UART_SOF_2  0x55

typedef enum {
    MSG_TELEMETRY        = 0x01,
    MSG_ACTUATOR_CMD     = 0x02,
    MSG_SET_CONFIG       = 0x10,
    MSG_GET_CONFIG       = 0x11,
    MSG_CONFIG_DATA      = 0x12,
    MSG_GET_LOG_HEADER   = 0x20,
    MSG_LOG_HEADER_DATA  = 0x21,
    MSG_READ_LOG_ENTRY   = 0x22,
    MSG_LOG_ENTRY_DATA   = 0x23,
    MSG_CLEAR_LOGS       = 0x24,
    MSG_SIM_FAULT_INJECT = 0x30,
    MSG_ACK              = 0xF0,
    MSG_NAK              = 0xF1
} MessageId_t;

typedef struct __attribute__((packed)) {
    uint8_t  state;
    uint16_t temp_c_x10;
    uint16_t target_temp_c_x10;
    uint16_t cup_distance_mm;
    uint8_t  cup_present;
    uint8_t  strength_mode;
    uint8_t  heater_power_pct;
    uint8_t  pump_active;
    uint8_t  fault_code;
    uint32_t brew_time_remaining_ms;
} TelemetryPayload_t;

void uart_protocol_init(UART_HandleTypeDef *huart);
void uart_protocol_process(void);
void uart_protocol_send_telemetry(void);
void uart_protocol_on_rx_byte(uint8_t byte);
```

---

## 4. Memory Budget & Resource Allocation

| Resource | Available on STM32L475VG | Allocated by Firmware | Margin / Free |
| :--- | :---: | :---: | :---: |
| **Internal Flash (ROM)** | 1024 KB (1 MB) | $\approx 42\text{ KB}$ | $> 95\%$ Free |
| **Internal SRAM** | 128 KB (96K + 32K) | $\approx 8.5\text{ KB}$ | $> 93\%$ Free |
| **External QSPI Flash** | 8192 KB (8 MB) | $60\text{ KB}$ active | $> 99\%$ Free for logging / expansion |
