# STM32 IoT Coffee Maker

An embedded coffee-maker controller built around the **B-L475E-IOT01A1**
development board (STM32L475VGTX). The STM32 firmware reads physical controls
and sensors, runs the coffee-cycle state machine, drives status LEDs, and
reports telemetry to a Python desktop dashboard.

The heater and pump are **simulation-only outputs**. The project does not
control mains voltage or a physical heater or pump.

## System overview

- **Inputs:** PC13 power button, PD14 start/cancel button, PA3 strength
  selector, PC5 potentiometer for simulated water temperature, and the onboard
  VL53L0X time-of-flight sensor for cup detection.
- **Controller:** STM32 firmware with a HAL-free state machine and a
  superloop application that collects inputs, checks safety conditions,
  updates indicators, logs events, and sends status.
- **Outputs:** External ready, heating, and fault LEDs; onboard heartbeat LED;
  virtual heater and pump requests.
- **PC link:** USART1 at 115200 baud, 8N1, through the ST-LINK virtual COM
  port.
- **Persistent records:** Onboard QSPI flash stores CRC-protected event
  records and cup-detection calibration profiles.
- **Digital twin:** A read-only Tkinter dashboard displays live status,
  animated virtual brewing, board LED levels, and downloadable QSPI event
  records as CSV.

The normal coffee-cycle states are **STANDBY**, **IDLE**, **HEATING**,
**BREWING**, **COMPLETE**, and **FAULT**. Safety checks include cup presence,
temperature limits, sensor errors, and a heating timeout. Thresholds and
recovery rules are documented in the fault-matrix diagram.

## Run the digital twin

From the repository root, create and activate the project-local Conda
environment, then launch the dashboard:

```powershell
conda env create --prefix .\DigitalTwin\.conda-env --file .\DigitalTwin\environment.yml
conda activate .\DigitalTwin\.conda-env
python .\DigitalTwin\coffee_twin.py
```

Connect the board to the PC, select its ST-LINK virtual COM port, and click
**Connect**. The dashboard expects the coffee application to be running and
receiving USART1 `STATUS` telemetry. It can request QSPI event records and save
them as CSV. It does not send coffee-control commands to the board.

## Firmware and operating modes

Open `FinalProject_STM32/FinalProject_STM32.ioc` in STM32CubeIDE and build the
`FinalProject_STM32` project. Compile-time options are in
`FinalProject_STM32/Inc/app_config.h`:

- `IO_DIAGNOSTICS_MODE=1` selects the GPIO/ADC/I2C/QSPI bring-up diagnostics.
- `IO_TOF_CALIBRATION_MODE=1` selects the guided cup-sensor calibration.
- With both set to `0`, the integrated coffee application runs.

Only one mode should be enabled at a time. See
[`DigitalTwin/README.md`](DigitalTwin/README.md) for dashboard details and
[`PROJECT_HANDOFF.md`](PROJECT_HANDOFF.md) for configuration, wiring, and
bring-up notes.

## Validation status

The diagnostic GPIO bring-up has been confirmed on the physical board. The
firmware build has succeeded. Coffee-mode ranging, state behavior, QSPI
calibration persistence and log retrieval, and live dashboard operation still
require hardware verification. The QSPI log-reading command is read-only;
hardware behavior is not yet claimed as verified.

For diagrams, slide previews, and the project presentation guide, see
[`Documentation/PROJECT_PROGRESS_AND_PRESENTATION_GUIDE.md`](Documentation/PROJECT_PROGRESS_AND_PRESENTATION_GUIDE.md).
