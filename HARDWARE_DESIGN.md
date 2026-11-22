# STM32 IoT Coffee Maker — Hardware Design Specification

**Target Board**: STMicroelectronics B-L475E-IOT01A1 (Discovery kit for IoT node)  
**Target Microcontroller**: STM32L475VGT6 (ARM® Cortex®-M4F @ 80 MHz, 1 MB Flash, 128 KB SRAM, LQFP100)  
**Host Interface**: USB ST-LINK Virtual COM Port (USART1)  
**Revision**: 1.0  
**Date**: 2026-11-20  

---

## 1. Executive Summary

This document establishes the hardware design and wiring specifications for the physical controller of the **STM32 IoT Coffee Maker**. The controller runs on the STM32L475VGT6 MCU and interfaces with an external breadboard featuring tactile user controls, status LEDs, and an analog potentiometer simulating boiler temperature. High-voltage heater elements and water pumps are not connected to physical mains; instead, actuators are simulated in a companion Python digital twin communicating over a 115,200 baud serial channel.

All diagrams and detailed pin listings are maintained under `Documentation/`:
* [PIN_MAPPING.md](Documentation/PIN_MAPPING.md) — Comprehensive Pin Allocation & Electrical Ratings
* [PHYSICAL_WIRING.svg](Documentation/Diagrams/PHYSICAL_WIRING.svg) — Vector Graphic Breadboard Wiring Diagram
* [PHYSICAL_WIRING.md](Documentation/Diagrams/PHYSICAL_WIRING.md) — Breadboard Layout & Jumper Wiring Schedule
* [ELECTRICAL_SCHEMATIC.md](Documentation/Diagrams/ELECTRICAL_SCHEMATIC.md) — Subsystem Schematics & Component Calculations
* [SYSTEM_BLOCK_DIAGRAM.md](Documentation/Diagrams/SYSTEM_BLOCK_DIAGRAM.md) — High-Level Architecture & Signal Flow

---

## 2. Approved Master Pin Mapping

| Signal Description | STM32 Pin | Header & Pin | I/O Mode | Electrical Parameters | Target Hardware |
| :--- | :---: | :---: | :--- | :--- | :--- |
| **Power On/Off** | `PC13` | *Onboard* | Input (Ext PU) | Active-Low, 10 kΩ onboard PU to 3.3V | User Button B2 (Blue, `BUTTON_EXTI13`; Note: B1 is NRST Reset) |
| **Water Temp (Sim)** | `PC5` | **CN4 pin 1** (A0) | ADC1_IN14 | 0.0 V to 3.3 V analog input | 10 kΩ Potentiometer Wiper (20 °C – 110 °C) |
| **Start / Cancel** | `PD14` | **CN3 pin 3** (D2) | Input (Int PU) | Active-Low to GND, internal ~40 kΩ PU | External Pushbutton (NO) |
| **Normal / Strong** | `PA3` | **CN3 pin 5** (D4) | Input (Int PU) | Active-Low via 3-terminal SPDT selector: COM to PA3, one throw to GND, other throw open | External 2-position toggle switch |
| **Ready LED** | `PB4` | **CN3 pin 6** (D5) | Output PP | **150 Ω** (InGaN 525nm, $V_f=3.0\text{--}3.2\text{ V}$) or **330 Ω** (GaP 565nm) | External Green LED |
| **Heating / Brewing** | `PB1` | **CN3 pin 7** (D6) | Output PP | **330 Ω** resistor, $V_f=2.0\text{--}2.2\text{ V}$, $I\approx 3.6\text{ mA}$ | External Yellow LED |
| **Fault LED** | `PA4` | **CN3 pin 8** (D7) | Output PP | **330 Ω** resistor, $V_f=2.0\text{--}2.2\text{ V}$, $I\approx 3.9\text{ mA}$ | External Red LED |
| **Cup Distance SCL** | `PB10` | *Onboard* | I2C2_SCL | 100 kHz Standard Mode, 4.7 kΩ PU | Onboard VL53L0X ToF Sensor |
| **Cup Distance SDA** | `PB11` | *Onboard* | I2C2_SDA | 100 kHz Standard Mode, 4.7 kΩ PU | Onboard VL53L0X ToF Sensor |
| **VL53L0X Shutdown** | `PC6` | *Onboard* | Output PP | Active-Low enable (Driven HIGH) | Onboard VL53L0X XSHUT |
| **Heartbeat LED** | `PB14` | *Onboard* | Output PP | 3.3 V logic drive | Onboard Green LED2 |
| **QSPI Flash Bus** | `PE10`–`PE15` | *Onboard* | QUADSPI | Bank 1, 4-bit, up to 80 MHz | Onboard MX25R6435F 8 MB Flash |
| **PC UART TX** | `PB6` | *Onboard* | USART1_TX | 115200 baud, 8N1 | ST-LINK Virtual COM Port |
| **PC UART RX** | `PB7` | *Onboard* | USART1_RX | 115200 baud, 8N1 | ST-LINK Virtual COM Port |

---

## 3. Power Distribution & Grounding Architecture

### 3.1 Voltage Regulators & Current Budgets
* The B-L475E-IOT01A1 board is powered via the ST-LINK USB connector (5 V, up to 500 mA).
* Onboard step-down LDO regulator (LD39050PU33R) delivers regulated **+3.3 V DC** to the microcontroller and header **CN2 pin 4**.
* Total external breadboard current consumption:
  * Potentiometer voltage divider: $0.33\text{ mA}$
  * All 3 status LEDs illuminated: $\approx 8.91\text{ mA}$
  * Switch pull-up currents when active: $\approx 0.17\text{ mA}$
  * **Aggregate external current**: $< 10\text{ mA}$, leaving ample thermal headroom on the 500 mA regulator.

### 3.2 Common Ground Implementation
* A continuous common ground is maintained between **CN2 pin 6** (GND) on the development board and the negative power rail of the external breadboard.
* All external switches and LED cathodes terminate directly at this common ground rail to prevent ground loops and analog offset errors on the ADC.

---

## 4. Hardware Verification & Assembly Checklist

Prior to supplying power via USB:
1. **Silkscreen Verification**: Inspect the silkscreen markings on the B-L475E-IOT01A1 to confirm that:
   * Header **CN2 pin 4** is labeled `3V3`.
   * Header **CN2 pin 6** is labeled `GND`.
   * Header **CN4 pin 1** corresponds to `A0`.
   * Header **CN3 pin 3, 5, 6, 7, 8** correspond to `D2`, `D4`, `D5`, `D6`, `D7`.
2. **Resistance Check**: Disconnect USB power. Measure resistance between breadboard 3.3V rail and GND rail. Confirm no short circuit exists ($R > 1\text{ k}\Omega$).
3. **LED Polarity**: Ensure all LED anodes face the current-limiting series resistors and cathodes face the blue ground rail.
4. **Logic Level Integrity**: Ensure no signals exceed 3.3 V. (The STM32L475 is a 3.3 V device; all external breadboard wiring must derive strictly from CN2 Pin 4).
