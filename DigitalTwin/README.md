# Coffee Maker Digital Twin

This read-only desktop dashboard listens to the STM32 USART1 status line and animates a coffee maker, the four board LED outputs, virtual heater/pump requests, sensor readings, and controller state. During brewing it shows a pump-on-time timer and a virtual mug that fills as the pump runs; the mug level is illustrative and is not measured by a liquid sensor.

## Run

1. From the repository root, create the project-local Miniconda environment: `conda create --prefix DigitalTwin/.conda-env python=3.12 pyserial --yes`.
2. Activate it in PowerShell: `conda activate .\DigitalTwin\.conda-env`.
3. Build and flash the STM32 coffee application with both `IO_DIAGNOSTICS_MODE` and `IO_TOF_CALIBRATION_MODE` set to `0` in `FinalProject_STM32/Inc/app_config.h`. GPIO diagnostics and ToF calibration modes do not service the QSPI log request.
4. Connect to the ST-LINK virtual COM port at **115200 baud, 8N1** and launch `python DigitalTwin/coffee_twin.py`.
5. Select the COM port and click **Connect**. The dashboard accepts lines beginning with `STATUS,`; diagnostic `DIAG` lines are ignored.

The dashboard marks telemetry stale after 500 ms and stops its virtual heater/pump display. **Read QSPI log** sends the read-only `CMD,READ_LOGS` request over USART1; the STM32 replies with `LOG_SCAN`, scans eight slots per task pass while continuing the coffee task, then streams CRC-valid event records into a table that can be saved as CSV. The GUI reports a timeout if it receives no scan response. This command does not erase or program flash. The `flash` STATUS field reports that the QSPI chip/interface initialized; it does not represent recent I/O health. The `logger` field reports whether event appends are available. Log reads remain available after an event-append error, and logger recovery scans also run in bounded chunks; retries start every 5 seconds. Hardware retrieval still needs validation. The reservoir fill graphic is illustrative because the hardware has no tank-level sensor. Heater and pump values are simulation outputs only. `requirements.txt` is also provided for non-conda installs (`python -m pip install -r DigitalTwin/requirements.txt`).

## Current telemetry fields

The controller sends `state`, `temp_x10`, `target_x10`, `cup_mm`, `cup_valid`, `cup`, `strength`, `heater`, `pump`, `ready_led`, `heating_led`, `fault_led`, `heartbeat_led`, `flash`, `logger`, and `fault` at 10 Hz. `cup_mm` is shown only while `cup_valid=1`; it is cleared when ranging fails or the ToF is being reinitialized, so an old distance is not displayed as a live reading. LED fields are the current logical output levels, so the dashboard mirrors the MCU indicators rather than inferring them from state names. `flash=OK` means the QSPI chip/interface initialized; it does not certify recent read, erase, or program health. `logger=OK` means event appends are enabled. Neither field replaces hardware validation.
