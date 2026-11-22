# STM32 IoT Coffee Maker — Project Brief

## Hardware
- Board: B-L475E-IOT01A1 (STM32L475VG)
- IDE: STM32CubeIDE 2.2.0
- Configuration: STM32CubeMX
- Firmware: C, STM32 HAL, bare-metal superloop
- Development OS: Windows

## Project objective
Build an IoT coffee-maker controller using a physical STM32 board and a PC-based Python digital twin.

The STM32 executes all control logic. Python simulates the coffee machine's water tank, heater, brewing process, and user interface.

## Inputs
- Onboard user button B2 (`BUTTON_EXTI13`, PC13): power toggle (active-low)
- External pushbutton: start/cancel (PD14, active-low)
- Three-terminal, two-position strength selector: normal/strong coffee (PA3, active-low; SPDT common to PA3, one throw to GND, other throw open)
- Potentiometer: simulated water temperature (PC5, ADC1_IN14, mapped 20 °C – 110 °C)
- VL53L0X: cup detection (onboard I2C2, PB10/PB11, XSHUT on PC6)
- Additional onboard sensors: evaluate for useful functionality

## Outputs
- External LEDs: ready (PB4), heating/brewing (PB1), fault (PA4)
- Onboard LEDs: heartbeat / status (LED2, PB14)
- UART communication with PC (USART1, PB6/PB7 @ 115200 8N1 via ST-LINK VCP)

## Planned peripherals
- GPIO
- ADC (ADC1_IN14 on PC5)
- I2C (I2C2 on PB10/PB11)
- SPI / QSPI (Onboard Macronix MX25R6435F 8 MB Quad-SPI NOR Flash fulfills the SPI-family peripheral requirement)
- UART (USART1 on PB6/PB7)

All pin assignments must be verified against the board schematic and CubeMX configuration before implementation.

## Firmware architecture
- Button manager (PC13, PD14, PA3 with strength latching)
- LED manager (PB4, PB1, PA4, PB14)
- Coffee state machine
- Temperature sensor (EMA filter, 20 °C – 110 °C)
- Cup sensor (VL53L0X driver via I2C2)
- QSPI flash driver & storage manager (ping-pong config & 32-byte circular event logging)
- UART protocol (binary framing with CRC16, telemetry, commands, log streaming)
- Fault manager (safety interlocks, unified fault matrix)

## State machine
STANDBY ↔ IDLE → HEATING → BREWING → COMPLETE → IDLE

FAULT can be entered from any operating state.

## Safety
- No mains voltage or real heater/pump control.
- Simulate actuators in Python.
- Fail-safe timeout: Python twin cuts virtual heater/pump if telemetry lost for > 500 ms.
- Keep STM32 GPIO at 3.3 V logic.
- Use nonblocking timing with HAL_GetTick().
- Avoid blocking delays in the control loop.

## Development rules
- Never modify the .ioc file without explicit approval.
- Never modify generated initialization code without explicit approval.
- Preserve STM32CubeIDE compatibility.
- Use separate modules where practical.
- Build and test after each implementation stage.
- Document assumptions and unresolved hardware questions.

## Hardware Verification & Decision Log
- **D1 (Temperature Source)**: Physical potentiometer on PC5 is the primary sensor for Milestone 1. Software injection via Python twin is supported with a 1000 ms staleness timeout.
- **D2 (Over-Temp Reachability)**: ADC mapped to 20 °C – 110 °C so that the 102 °C fault threshold can be verified at ~91% potentiometer rotation.
- **D4 (SPI Requirement)**: Fulfilled by the onboard 64-Mbit Macronix MX25R6435F Quad-SPI NOR Flash on PE10–PE15.
- **D6 (Safety & Watchdog)**: Python twin watchdog enforces 500 ms fail-safe cutoff. STM32 IWDG configured for 1000 ms timeout refreshed in superloop.
- **Additional Onboard Sensors**: HTS221, LPS22HB, LSM6DSL, and LIS3MDL all share the I2C2 bus. Evaluated for machine tilt and vibration monitoring; deferred to Milestone 2 to maintain lean superloop timing.
- **Pushbutton Silkscreen Verification**: Verified against board schematic: B2 (Blue) is `BUTTON_EXTI13` on PC13. B1 (Black) is NRST hardware reset.
