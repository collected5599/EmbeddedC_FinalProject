# STM32 IoT Coffee Maker — System Architecture Specification

**Target Hardware**: STMicroelectronics B-L475E-IOT01A1 (Discovery kit for IoT node)  
**Microcontroller**: STM32L475VGT6 (ARM® Cortex®-M4F @ 80 MHz, 1 MB Flash, 128 KB SRAM)  
**External Non-Volatile Memory**: Macronix MX25R6435F 64-Mbit (8 MB) Quad-SPI NOR Flash  
**Companion Host**: PC Python Digital Twin via ST-LINK VCP (USART1 @ 115,200 baud)  
**Revision**: 1.2  
**Date**: 2026-11-20  

---

## 1. System Overview & Architecture Philosophy

The **STM32 IoT Coffee Maker** is an embedded cyber-physical system split cleanly between:
1. **The Physical Embedded Controller (STM32L475VG)**: Executes all deterministic real-time control logic, sensor acquisition, button debouncing, LED visual signaling, state transitions, fault interlocks, and non-volatile flash storage.
2. **The External Breadboard I/O**: Provides physical user controls (buttons, switches, potentiometer) and status indicators (LEDs).
3. **The PC Python Digital Twin**: Simulates the physical thermodynamics of the water tank, the water reservoir depletion, and the high-voltage physical actuators (heater heating element and high-pressure pump) across a robust serial telemetry link.

```mermaid
flowchart TB
    subgraph Physical_Hardware ["Physical Hardware Layer"]
        BB["External Breadboard<br/>• Potentiometer (PC5)<br/>• Pushbutton (PD14)<br/>• Lever Switch (PA3)<br/>• 3x Status LEDs (PB4, PB1, PA4)"]
        BOARD["B-L475E-IOT01A1 Board<br/>• User Button B2 (PC13, BUTTON_EXTI13)<br/>• Heartbeat LED2 (PB14)<br/>• VL53L0X ToF Cup Sensor (I2C2)<br/>• MX25R6435F 8 MB Flash (QUADSPI)"]
    end

    subgraph Firmware_Layer ["STM32 Firmware (Bare-Metal Superloop)"]
        DRV["Hardware Abstraction & Drivers<br/>• button_manager • led_manager<br/>• temp_sensor • cup_sensor<br/>• qspi_flash"]
        LOGIC["Core Logic & Storage<br/>• coffee_sm (State Machine)<br/>• fault_manager (Safety Supervisor)<br/>• storage_manager (Ping-Pong Config & Circular Log)"]
        COMM["Communication Subsystem<br/>• uart_protocol (Framing, CRC, Commands)"]
    end

    subgraph Host_Layer ["Host PC Layer"]
        TWIN["Python Digital Twin Application<br/>• Thermal Boiler Simulator<br/>• Virtual Pump & Heater Actuators<br/>• Real-time Telemetry Dashboard<br/>• Historical Log Downloader & Inspector"]
    end

    Physical_Hardware <-->|GPIO / ADC / I2C / QSPI| Firmware_Layer
    Firmware_Layer <-->|USART1 over ST-LINK USB (115200 8N1)| Host_Layer
```

---

## 2. Hardware Subsystem & Interface Architecture

### 2.1 Microcontroller Resources & Peripheral Map
The STM32L475VGT6 is clocked at 80 MHz via its internal Phase-Locked Loop (PLL) fed by the Multi-Speed Internal (MSI) oscillator.

| Subsystem | Peripheral | Pins Assigned | Description / Hardware Interconnect |
| :--- | :--- | :--- | :--- |
| **User Inputs** | GPIO Inputs | `PC13`, `PD14`, `PA3` | Power button B2 (`BUTTON_EXTI13`), Start/Cancel button (external), Normal/Strong lever (external). All active-low. |
| **Water Temp Sim** | ADC1_IN14 | `PC5` (CN4 pin 1) | Analog voltage ($0.0\text{ V} \le V \le 3.3\text{ V}$) from 10 kΩ potentiometer (scaled 20 °C – 110 °C). |
| **Cup Detection** | I2C2 + GPIO | `PB10` (SCL), `PB11` (SDA), `PC6` (XSHUT) | Onboard ST VL53L0X Time-of-Flight ranging sensor (100 kHz I2C). |
| **External LEDs** | GPIO Outputs | `PB4`, `PB1`, `PA4` | Ready (Green, 150 Ω), Heating (Yellow, 330 Ω), Fault (Red, 330 Ω). |
| **Onboard Status** | GPIO Output | `PB14` | Onboard Green LED2 for superloop heartbeat / diagnostic pulse. |
| **Flash Storage** | QUADSPI | `PE10`–`PE15` | Dedicated onboard 64-Mbit (8 MB) Macronix MX25R6435F NOR Flash memory. |
| **Host Serial Link**| USART1 | `PB6` (TX), `PB7` (RX) | Routed to onboard ST-LINK VCP (115,200 baud, 8 data bits, no parity, 1 stop bit). |

---

## 3. QSPI Flash Storage Subsystem Architecture

The **Macronix MX25R6435F** is an 8 Megabyte (64 Mbit) serial NOR flash partitioned into 2,048 sectors of 4 Kilobytes each. To achieve maximum reliability, wear-leveling, and power-loss immunity without third-party library overhead, a **Ping-Pong Sector Architecture with Circular Binary Logging** is implemented.

```text
    ==================== MX25R6435F (8 MB) FLASH MEMORY MAP ====================
    Address Range         Sectors     Size    Function
    ----------------------------------------------------------------------------
    0x000000 - 0x000FFF   Sector 0    4 KB    Config Sector A (Primary)
    0x001000 - 0x001FFF   Sector 1    4 KB    Config Sector B (Mirror / Alternate)
    0x002000 - 0x002FFF   Sector 2    4 KB    Lifetime Machine Statistics & Counters
    0x003000 - 0x00FFFF   Sect 3-15   52 KB   Circular Black-Box Event Log (1,664 entries)
    0x010000 - 0x7FFFFF   Sect 16+    ~7.9 MB Reserved / Staging / Audio Assets
    ============================================================================
```

### 3.1 Power-Loss Safe Ping-Pong Configuration Storage
Configuration parameters (target water temperature, pre-infusion duration, brew pump duration, cup distance threshold) are stored in a structured header:
1. When updating settings, the inactive sector is erased and written with the new configuration payload, incremented revision counter, and CRC32 checksum.
2. The active pointer is updated only upon successful write and read-back CRC verification.
3. If power is interrupted mid-write, the firmware detects the incomplete CRC on boot and automatically falls back to the previous valid sector.

### 3.2 Circular Event & Diagnostic Log Buffer
Sectors 3 through 15 (52 KB total) store consecutive **32-byte binary log entries**.
* Capacity: 13 sectors $\times$ 128 entries = 1,664 entries total. Once wrapped, the oldest sector is pre-erased, guaranteeing a minimum of 12 $\times$ 128 = **1,536 active historical entries**.
* **Head/Tail Recovery**: Each 32-byte record includes a monotonic 32-bit sequence counter (`seq`). On boot, the firmware performs a sequential linear scan across all 13 sectors (checking up to 1,664 slots), skipping unprogrammed entries (`0xFFFFFFFF`), validating the CCITT-FALSE CRC16 on populated records, and tracking the highest valid sequence number to locate the circular write head and tail. This guarantees deterministic recovery even across wrapped buffers.
* **Pre-Erase Policy**: To guarantee zero blocking at the moment a critical fault occurs, the next sector is pre-erased during background idle time, never during a fault log write.
* **Flash Read Arbitration**: While an in-flight background erase or program is active (`qspi_flash_is_busy() == true`), read requests return `QSPI_BUSY` and external UART log queries are answered with `NAK_DEVICE_BUSY` (0x05) to avoid reading invalid internal state.

---

## 4. Finite State Machine (FSM) Architecture

The coffee maker controller operates as a deterministic, hierarchical Finite State Machine managed by `coffee_sm`:

```mermaid
stateDiagram-v2
    [*] --> STANDBY : Power Applied

    STANDBY --> IDLE : Power Button Pressed (PC13 - B2)
    IDLE --> STANDBY : Power Button Pressed (PC13 - B2)

    IDLE --> HEATING : Start Button Pressed (PD14) [Cup Present & Temp < Target]
    IDLE --> BREWING : Start Button Pressed (PD14) [Cup Present & Temp >= Target]

    HEATING --> BREWING : Water Temp >= Target Temp [Cup Present]
    HEATING --> IDLE : Cancel Button Pressed (PD14)
    HEATING --> FAULT : Cup Removed (Distance >= 100 mm for 200 ms) OR Heating Timeout (> 180s)

    BREWING --> COMPLETE : Brew Timer Expired
    BREWING --> FAULT : Cup Removed (Distance >= 100 mm for 200 ms)
    BREWING --> IDLE : Cancel Button Pressed (PD14) [Logged as EVT_USER_CANCEL]

    COMPLETE --> IDLE : Cup Removed OR 5s Timeout

    IDLE --> FAULT : Over-Temp (>102°C) OR Sensor Failure OR Tank Empty
    HEATING --> FAULT : Over-Temp (>102°C) OR Sensor Failure OR Tank Empty
    BREWING --> FAULT : Over-Temp (>102°C) OR Sensor Failure OR Tank Empty
    COMPLETE --> FAULT : Over-Temp (>102°C)
    
    FAULT --> IDLE : Start/Cancel Button Pressed [Specific Fault Cleared]
    FAULT --> STANDBY : Power Button Pressed [Enters Standby; next power-on boots to IDLE]
```

### 4.1 State Definitions & Invariants
* **`STATE_STANDBY`**: Machine powered off logically. Main indicators OFF; onboard Green LED2 (`PB14`) blinks a slow heartbeat (1 Hz) to confirm loop health. QSPI flash placed in deep power-down mode (`qspi_flash_deep_power_down()`). Only PC13 power button is processed.
* **`STATE_IDLE`**: Ready for operation. Ready LED (Green `PB4`) illuminates solid ONLY when cup is present ($d < 80\text{ mm}$) AND water temperature $\ge$ target ($92^\circ\text{C}$ or $95^\circ\text{C}$). If Start (PD14) is pressed without a cup, the command is ignored and logged as `EVT_USER_START_IGNORED`.
* **`STATE_HEATING`**: Target temperature commanded to Python twin heater simulation. Yellow LED (`PB1`) blinks at 1 Hz. Cup presence is actively monitored. If cup is removed for $\ge 200\text{ ms}$, transitions immediately to `FAULT_NO_CUP`.
* **`STATE_BREWING`**: Target temperature satisfied. Executes a two-phase brew cycle:
  1. **Pre-infusion phase**: Virtual pump runs for `preinfusion_time_s` (default: 2 s normal, 5 s strong), then pauses for 2 s to bloom the coffee bed.
  2. **Extraction phase**: Virtual pump runs for the remainder of `brew_time_s` (20 s normal, 32 s strong).
  Yellow LED is illuminated solid throughout. Lever switch is latched at brew start; lever toggles during active brewing are ignored until the cycle completes.
* **`STATE_COMPLETE`**: Pump commanded OFF. Green and Yellow LEDs blink synchronously for 5 s. Completion record written to QSPI flash log. Lifetime brew count incremented in Sector 2.
* **`STATE_FAULT`**: Triggered immediately on safety interlock violation. Red LED (`PA4`) flashes rapidly at 4 Hz. Virtual pump and heater immediately shut down BEFORE enqueuing fault log. Machine latches in fault until manually cleared:
  * Pressing Start/Cancel (PD14) evaluates whether the **specific active fault condition** has cleared (e.g., cup returned for `FAULT_NO_CUP`, temp $< 80.0^\circ\text{C}$ for `FAULT_OVERTEMP`, water tank refilled for `FAULT_TANK_EMPTY`). If cleared, `fault_manager_clear_fault()` is invoked and the state machine transitions to `STATE_IDLE`.
  * Pressing the Power Button (PC13 - B2) transitions from `STATE_FAULT` directly to `STATE_STANDBY`. Upon subsequent power-up, hardware re-initializes cleanly.

---

## 5. Safety Interlocks & Unified Fault Matrix

Safety is managed continuously by the independent `fault_manager` module running every **20 ms** in the superloop:

| Code | Fault Identifier | Trigger Condition | Latching? | Recovery Procedure |
| :---: | :--- | :--- | :---: | :--- |
| **0** | `FAULT_NONE` | Normal operation | No | N/A |
| **1** | `FAULT_NO_CUP` | Cup distance $\ge 100\text{ mm}$ for $\ge 200\text{ ms}$ during `HEATING` or `BREWING` | Yes | Place cup on tray ($d < 80\text{ mm}$); press Start/Cancel (PD14) |
| **2** | `FAULT_OVERTEMP` | Boiler temperature $> 102.0^\circ\text{C}$ | Yes | Cool below $80.0^\circ\text{C}$; press Start/Cancel (PD14) |
| **3** | `FAULT_HEAT_TIMEOUT` | `HEATING` state exceeds $180\text{ s}$ without reaching target | Yes | Verify water/heater simulation; press Start/Cancel (PD14) |
| **4** | `FAULT_SENSOR_COMM` | 3 consecutive I2C read transaction failures to VL53L0X | Yes | Start/Cancel attempts re-init; latches again if bus remains unresponsive |
| **5** | `FAULT_TANK_EMPTY` | Injected by Python digital twin simulation | Yes | Refill virtual tank in Python twin; press Start/Cancel (PD14) |

> [!NOTE]
> **User Cancelation Semantics**: Pressing the external pushbutton (PD14) during active brewing or heating immediately aborts the pump/heater and transitions cleanly to `STATE_IDLE`. It is non-latching and logged as an event (`EVT_USER_CANCEL`), not a fault.

---

## 6. UART Communication Protocol & Frame Format

The STM32 controller and Python Digital Twin communicate via **packetized binary frames with CRC16** over USART1 at 115,200 baud (8N1).

### 6.1 Packet Structure

```text
+--------+--------+--------+--------+---------+ ... +---------+--------+--------+
| SOF 1  | SOF 2  | MSG_ID | LENGTH | DATA 0  | ... | DATA N  | CRC16L | CRC16H |
|  0xAA  |  0x55  | (1 B)  | (1 B)  | (0-64B) |     | (0-64B) | (LSB)  | (MSB)  |
+--------+--------+--------+--------+---------+ ... +---------+--------+--------+
```

* **Start of Frame (SOF)**: `0xAA`, `0x55` (2 bytes)
* **Message ID (`MSG_ID`)**: Command or Telemetry identifier (1 byte)
* **Payload Length (`LENGTH`)**: Number of data bytes (0 to 64 bytes)
* **Data Payload**: Message-specific payload (Little-Endian)
* **Checksum (`CRC16`)**: Standard CCITT-FALSE CRC (Polynomial `0x1021`, Initial `0xFFFF`), computed over `MSG_ID` + `LENGTH` + `DATA[0..N]`.

### 6.2 Message Catalog

| Message Name | ID | Direction | Description |
| :--- | :---: | :---: | :--- |
| `MSG_TELEMETRY` | `0x01` | STM32 $\rightarrow$ PC | Periodic 10 Hz telemetry: Current state, water temp, target temp, cup distance, cup present, strength mode, heater power %, pump active, fault code. |
| `MSG_ACTUATOR_CMD` | `0x02` | STM32 $\rightarrow$ PC | Event-driven actuator command: Heater Power (0–100%), Pump State (ON/OFF). |
| `MSG_SET_CONFIG` | `0x10` | PC $\rightarrow$ STM32 | Update target temperature, brew durations, cup thresholds (persisted to QSPI flash). |
| `MSG_GET_CONFIG` | `0x11` | PC $\rightarrow$ STM32 | Request current active configuration from QSPI flash. |
| `MSG_CONFIG_DATA` | `0x12` | STM32 $\rightarrow$ PC | Payload containing full active `MachineConfig_t` struct. |
| `MSG_GET_LOG_HEADER` | `0x20` | PC $\rightarrow$ STM32 | Request total log count, head sequence number, tail sequence number. |
| `MSG_LOG_HEADER_DATA`| `0x21` | STM32 $\rightarrow$ PC | Header response: total entries, head index, tail index, sequence number. |
| `MSG_READ_LOG_ENTRY` | `0x22` | PC $\rightarrow$ STM32 | Request specific log entry by relative index. |
| `MSG_LOG_ENTRY_DATA` | `0x23` | STM32 $\rightarrow$ PC | 32-byte binary log entry payload. |
| `MSG_CLEAR_LOGS` | `0x24` | PC $\rightarrow$ STM32 | Erase circular event log sectors in QSPI flash. Requires confirmation token `0xC0FFEE01`. |
| `MSG_SIM_FAULT_INJECT`| `0x30`| PC $\rightarrow$ STM32 | Digital twin injects simulated condition (Payload: `uint8_t fault_code`). Gated behind debug mode. |
| `MSG_INJECT_TEMP` | `0x31` | PC $\rightarrow$ STM32 | Digital twin streams simulated temperature (Payload: `uint16_t temp_c_x10`). 1000 ms timeout. |
| `MSG_ACK` | `0xF0` | Both | General acknowledgment (Payload: `uint8_t acked_msg_id`). |
| `MSG_NAK` | `0xF1` | Both | Rejection packet (Payload: `uint8_t nacked_msg_id`, `uint8_t err_code`). |

### 6.3 NAK Error Codes
* `0x01`: `NAK_BAD_CRC` — Checksum verification failed.
* `0x02`: `NAK_BAD_LEN` — Frame length outside allowable bounds (0–64 bytes).
* `0x03`: `NAK_UNKNOWN_CMD` — Unrecognized message identifier.
* `0x04`: `NAK_OUT_OF_BOUNDS` — Parameter exceeds valid range (e.g., target temp > 100 °C).
* `0x05`: `NAK_DEVICE_BUSY` — Flash or peripheral busy; request deferred.
* `0x06`: `NAK_BAD_TOKEN` — Confirmation token mismatch (e.g., invalid clear-log token).

### 6.4 Host Fail-Safe Timeout
* **Dead-Man's Timer**: If the Python digital twin does not receive a valid `MSG_TELEMETRY` packet for **500 ms**, the Python twin automatically cuts power to its virtual heater and pump models, ensuring safe shutdown in the event of an STM32 crash or serial disconnection.
* **Injected Temperature Staleness**: If `temp_sensor` is configured in `TEMP_SOURCE_TWIN_SIMULATION` mode and no `MSG_INJECT_TEMP` packet is received for $> 1000\text{ ms}$, the firmware raises `FAULT_SENSOR_COMM` or safely falls back to the physical potentiometer reading.
