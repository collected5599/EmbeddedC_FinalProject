# Project Handoff Notes

Read this file first when continuing work on the STM32 IoT Coffee Maker project.

## User intent

Continue the coffee-maker firmware work after Antigravity ran out of tokens. The user wants to write and develop the coffee logic before flashing. They also want durable progress documentation and a block diagram for a screenshot-based presentation modeled on `UCSD_ex_Embedded_C_Assignment_8_I2C_Adithya_Naresh.pptx`.

## Board and project

- Board: B-L475E-IOT01A1, MCU STM32L475VGTX.
- STM32CubeIDE/CubeMX project: `FinalProject_STM32/`.
- CubeMX I/O project: `FinalProject_STM32/FinalProject_STM32.ioc`.
- Documentation summary and slide outline: `Documentation/PROJECT_PROGRESS_AND_PRESENTATION_GUIDE.md`.
- System and state diagrams: `Documentation/Diagrams/COFFEE_MAKER_FUNCTIONAL_BLOCK_DIAGRAM.md`.
- The physical breadboard wiring drawing was reviewed against the pin map; its external signal assignments are correct. See `Documentation/Diagrams/PHYSICAL_WIRING.md` and `PHYSICAL_WIRING.svg`.

## Confirmed CubeMX configuration

- ADC1_IN14 on PC5; Rank 1; 12-bit; sample time 92.5 cycles; label `A0_TEMP_POT`.
- I2C2 Standard mode (100 kHz): PB10 SCL, PB11 SDA.
- USART1 async 115200 8N1: PB6 TX, PB7 RX.
- IWDG stays enabled (prescaler 32, reload 1000). The user explicitly requested refresh code instead of toggling IWDG in CubeMX. `Src/main.c` now calls `HAL_IWDG_Refresh(&hiwdg)` at the end of the superloop. Preserve this and keep it after application work in future.
- User labels for GPIOs were updated in CubeMX; generated `main.h` exposes labels such as `D2_BTN_START_CANCEL`, `D4_SW_STRENGTH`, `D5_LED_READY`, `D6_LED_HEATING`, `D7_LED_FAULT`, `VL53L0X_XSHUT`, and `LED_HEARTBEAT`.

## Hardware and wiring notes

- Breadboard: CN2 pin 4 3.3 V to red rail; CN2 pin 6 GND to blue rail.
- Potentiometer: 10 kΩ between rails; wiper to CN4 pin 1 PC5/A0. A 100 nF capacitor from PC5/wiper to GND is recommended; the drawing mentions it but does not show the capacitor.
- Start/cancel button: CN3 pin 3 PD14/D2 to momentary switch to GND.
- Strength switch: CN3 pin 5 PA3/D4 to SPST switch to GND.
- Green/yellow/red LEDs: CN3 pins 6/7/8 (PB4/PB1/PA4), each with its series resistor (150/330/330 Ω) and cathode to GND.
- The VL53L0X sensor and PC13 user button are onboard; do not wire them to the breadboard.
- Wire while the board is unplugged. Use printed header labels rather than counting pins from a drawing.

## Current code changes

- Added HAL-free FSM API: `FinalProject_STM32/Inc/coffee_sm.h`.
- Added FSM implementation: `FinalProject_STM32/Src/coffee_sm.c`.
- FSM covers standby, idle, heating, three brew phases, complete, latched faults, cancel, fault recovery, LED patterns, and simulated actuator requests.
- The FSM module is not yet integrated with `main.c` or physical drivers. It accepts already-qualified sensor/button inputs. No VL53L0X driver or UART protocol implementation exists yet.
- `main.c` was changed only inside the CubeMX `USER CODE BEGIN 3` loop area for watchdog refresh; generated peripheral initialization was not edited.
- No build or hardware test has been run after these changes.

## Assumptions and next work

- First milestone: potentiometer provides temperature; Python twin simulates heater and pump.
- Next: inspect FSM for integration requirements, implement debounced button and ADC temperature drivers, then VL53L0X cup driver, output/LED manager, and UART protocol. Connect modules in the superloop; refresh IWDG only after a successful loop pass.
- Do not modify the `.ioc` unless the user explicitly requests it. Retain CubeMX-generated code edits only inside `USER CODE` sections.
- Update the progress guide and this handoff note as work advances. Capture CubeMX pinout, I2C, GPIO, build, and hardware test screenshots for the presentation.
