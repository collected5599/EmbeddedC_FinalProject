# STM32 IoT Coffee Maker — Verification Plan & Test Results

**Target Platform**: B-L475E-IOT01A1 (STM32L475VGT6)  
**Host Application**: Python Digital Twin  
**Document Revision**: 1.0  
**Date**: 2026-11-20  

---

## 1. Test Verification Matrix

| Test ID | Subsystem | Test Description | Execution Method & Procedure | Pass Criteria | Status |
| :---: | :--- | :--- | :--- | :--- | :---: |
| **T1** | **Physical Wiring** | Pre-power electrical and wiring validation. | Disconnect power. Measure resistance between 3.3V and GND rails. Verify LED polarities and resistor color codes. Verify header labels. | Resistance $> 1\text{ k}\Omega$; all cathodes to GND rail; silkscreen labels verified. | PENDING |
| **T2** | **Clock & Loop Timing**| Superloop cadence and non-blocking health check. | Flash baseline firmware. Observe onboard Green LED2 (`PB14`). Measure max loop execution time. | Heartbeat blinks at exactly 1.0 Hz (500 ms toggle); max loop execution time $\le 5.0\text{ ms}$. | PENDING |
| **T3** | **Button Manager** | Tactile button debouncing and strength latching. | Press and release B2 (`PC13`), Start/Cancel (`PD14`), and flip Lever (`PA3`). Flip lever during active brew simulation. | No switch bounce events detected; strength mode latches on Start and ignores mid-brew toggle. | PENDING |
| **T4** | **LED Manager** | LED pattern generation and non-blocking timing. | Test `LED_OFF`, `LED_ON`, `LED_BLINK_1HZ`, `LED_BLINK_4HZ` across external LEDs (`PB4`, `PB1`, `PA4`). | Frequencies match $\pm 5\%$; no visual jitter; zero `HAL_Delay` calls. | PENDING |
| **T5** | **ADC Potentiometer** | Temperature sensor scaling and calibration. | Sweep potentiometer from minimum to maximum. Observe telemetry. Verify $92^\circ\text{C}$ at ~80% rotation and $> 102^\circ\text{C}$ at ~91% rotation. | Smooth reading from $20.0^\circ\text{C}$ to $110.0^\circ\text{C}$; EMA filter removes noise; over-temp triggers at $102^\circ\text{C}$. | PENDING |
| **T6** | **Cup Detection ToF** | VL53L0X ranging, hysteresis, and error handling. | Place cup at 50 mm, withdraw to 120 mm. Pull XSHUT (`PC6`) low in firmware debug build to simulate bus fault. | Present qualified at $< 80\text{ mm}$, Absent at $\ge 100\text{ mm}$; 3 failed reads assert `FAULT_SENSOR_COMM`. | PENDING |
| **T7** | **UART Protocol** | Packet framing, CRC16 validation, and telemetry. | Stream 10 Hz `MSG_TELEMETRY` packets to Python twin. Send malformed packets, corrupted CRCs, and rapid commands. | Zero frame drops at 115,200 baud; corrupted packets rejected with `MSG_NAK`; parser recovers immediately. | PENDING |
| **T8** | **QSPI Flash Driver**| Hardware communication with MX25R6435F. | Read JEDEC ID; configure High-Performance mode; verify `QE` bit; perform 4 KB sector erase, page write, and Quad Fast Read. | JEDEC ID matches `0xC22817`; Quad Read verified against written buffer; non-blocking polling succeeds. | PENDING |
| **T9** | **Config Persistence**| Ping-pong configuration recovery and power cut. | Write new configuration; simulate power loss during sector erase/write; reboot and verify active sector. | Valid configuration with correct CRC32 recovered from alternate sector upon power restore. | PENDING |
| **T10**| **Circular Event Log**| Event logging, buffer wrap, and sequence recovery. | Generate 2,000 synthetic events to trigger buffer wrap past 13 sectors (52 KB). Reboot and perform linear scan recovery. | Head and tail located by sequence number (`seq`); oldest sector cleanly pre-erased; no corrupted entries. | PENDING |
| **T11**| **FSM Transitions** | Complete coffee lifecycle state transitions. | Power on $\rightarrow$ `IDLE` $\rightarrow$ `HEATING` $\rightarrow$ `BREWING` $\rightarrow$ `COMPLETE` $\rightarrow$ `IDLE`. Verify pre-infusion and extraction. | Pre-infusion runs for configured duration, pauses 2s, then extracts; all transitions match specification. | PENDING |
| **T12**| **Safety Interlocks**| Unified fault matrix verification. | Trigger: (1) Cup removed mid-brew, (2) Over-temp $> 102^\circ\text{C}$, (3) Heating timeout $> 180\text{ s}$, (4) Tank empty simulation. | Immediate pump/heater shutdown; Red LED flashes 4 Hz; fault logged to QSPI; latches until cleared. | PENDING |
| **T13**| **Host Fail-Safe** | Python Digital Twin communication loss timeout. | Halt STM32 in debugger or hold B1 (NRST) during simulated active brewing. | Python digital twin shuts down virtual heater and pump within $500\text{ ms}$ of lost telemetry. | PENDING |
| **T14**| **STANDBY Mode** | Low-power standby state behavior. | Press B2 (`PC13`) from `IDLE` or `FAULT`. Observe power draw and indicators. | All main LEDs turn OFF; heartbeat LED continues blinking 1 Hz; QSPI enters deep power-down mode. | PENDING |
| **T15**| **Start Without Cup**| Interlock on start command in `IDLE`. | Press Start/Cancel (PD14) while cup is absent ($d \ge 100\text{ mm}$). | Machine remains in `IDLE`; Start command ignored and logged as `EVT_USER_START_IGNORED`. | PENDING |
| **T16**| **Config Validation**| Boundary validation and security token checks. | Send `MSG_SET_CONFIG` with target temp $> 100^\circ\text{C}$. Send `MSG_CLEAR_LOGS` with invalid token. | Rejections emit `MSG_NAK` with error code `NAK_OUT_OF_BOUNDS` and `NAK_BAD_TOKEN`; no flash write. | PENDING |
| **T17**| **Flash Arbitration**| Read request during background sector erase. | Request log entry over UART while a 4 KB sector pre-erase is in flight. | `storage_manager` defers or returns `NAK_DEVICE_BUSY`; zero corrupt bytes returned. | PENDING |
| **T18**| **Per-Fault Clear** | Verified per-fault recovery conditions. | Trip `FAULT_NO_CUP` at $92^\circ\text{C}$. Replace cup and press Start/Cancel. | Fault clears to `IDLE` immediately without requiring water to cool below $80^\circ\text{C}$. | PENDING |
| **T19**| **Stale Temp Input** | Host simulated temperature staleness timeout. | Set `TEMP_SOURCE_TWIN_SIMULATION` and halt telemetry packet stream for $> 1000\text{ ms}$. | Staleness detected; system asserts `FAULT_SENSOR_COMM` or safely falls back to potentiometer. | PENDING |
| **T20**| **Host FSM Unit Tests**| Pure-logic FSM test suite on PC. | Run unit test harness compiling `coffee_sm.c` on host PC with mock input structs. | 100% state coverage, boundary cases, and timer expirations pass on PC before flashing MCU. | PENDING |

---

## 2. Incremental Bring-Up & Test Sequence

Firmware modules will be implemented and validated in the following strict order:
1. **Stage 1 (Core & Heartbeat)**: GPIO configuration, `LED_HEARTBEAT` on PB14, SysTick non-blocking loop timing verification.
2. **Stage 2 (Inputs & Outputs)**: `button_manager` debouncing on PC13, PD14, PA3; `led_manager` pattern verification on PB4, PB1, PA4.
3. **Stage 3 (Analog Potentiometer)**: ADC1_IN14 sampling, offset calibration, EMA filtering, and temperature conversion.
4. **Stage 4 (Serial Telemetry)**: USART1 packet framing with CRC16, ring buffer reception, telemetry streaming to Python twin.
5. **Stage 5 (I2C ToF Sensor)**: VL53L0X distance measurement, threshold qualification, and bus timeout recovery.
6. **Stage 6 (QSPI NOR Flash)**: Macronix MX25R6435F JEDEC ID check, Quad Enable bit, non-blocking erase and page program.
7. **Stage 7 (Storage Subsystem)**: `MachineConfig_t` ping-pong storage, `LogEntry_t` 32-byte circular logging.
8. **Stage 8 (State Machine & Safety)**: `coffee_sm` lifecycle, `fault_manager` safety interlocks, and full integration with the Python Digital Twin.
