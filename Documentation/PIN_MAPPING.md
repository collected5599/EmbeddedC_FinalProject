# STM32 IoT Coffee Maker — Pin Mapping & Hardware Specification

**Board**: STMicroelectronics B-L475E-IOT01A1 (Discovery kit for IoT node)  
**Microcontroller**: STM32L475VGT6 (ARM® Cortex®-M4F @ 80 MHz, 1024 KB Flash, 128 KB SRAM, LQFP100)  
**Document Revision**: 1.0  
**Date**: 2026-11-20  

---

## 1. Master Pin Mapping Table

| STM32 Pin | Arduino Connector & Pin | Signal Name | Peripheral / Function | I/O Mode | Target Hardware Component | Electrical Specifications |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **PC13** | *Onboard* | `BUTTON_EXTI13` | GPIO_EXTI13 | Input, Active-Low, Falling Edge | Onboard Pushbutton (B2 Blue) | Tactile switch to GND; external 10 kΩ pull-up & 100 nF cap to 3.3V. Used for **Power On/Off**. CubeMX label: `BTN_POWER`. (Note: B1 is Black NRST Reset). |
| **PC5** | **CN4 pin 1** (A0) | `ARD.A0-ADC` | ADC1_IN14 | Analog Input | Potentiometer Center Wiper | 10 kΩ Linear Potentiometer (Wiper: 0.0 V – 3.3 V; 100 nF filter cap to GND recommended). Simulates **Water Temperature** (20 °C – 110 °C). CubeMX label: `A0_TEMP_POT`. Already pre-configured by BSP as `ADC1_IN14`. |
| **PD14** | **CN3 pin 3** (D2) | `ARD.D2-INT0_EXTI14` | GPIO_EXTI14 | Input with Pull-Up, Falling Edge | External Pushbutton (Momentary NO) | Momentary pushbutton to GND, active-low. Used for **Start / Cancel**. CubeMX label: `D2_BTN_START_CANCEL`. |
| **PA3** | **CN3 pin 5** (D4) | `ARD.D4` | GPIO Input | Input with Pull-Up | External 3-terminal, 2-position SPDT strength selector | Connect PA3 to switch COM, one outer throw to GND, and leave the other throw unconnected. The grounded position reads Strong (low); the other position reads Normal (high via pull-up). Verify COM/throws with a continuity meter. CubeMX label: `D4_SW_STRENGTH`. Override BSP default (`GPIO_Output` → `GPIO_Input`). |
| **PB4** | **CN3 pin 6** (D5) | `ARD.D5-PWM` | GPIO Output | Output Push-Pull, Initial Low | External Green LED (**Ready**) | Series Resistor: **150 Ω** (for true-green InGaN 525nm, $V_f=3.0\text{--}3.2\text{ V}$) or **330 Ω** (for classic yellow-green 565nm, $V_f=2.1\text{ V}$). Cathode to GND. CubeMX label: `D5_LED_READY`. Override BSP default (`TIM3_CH1` → `GPIO_Output`). |
| **PB1** | **CN3 pin 7** (D6) | `ARD.D6-PWM` | GPIO Output | Output Push-Pull, Initial Low | External Yellow LED (**Heating/Brewing**) | Series Resistor: **330 Ω** to Anode. Cathode to GND. $V_f = 2.0\text{--}2.2\text{ V}$, $I \approx 3.6\text{--}3.9\text{ mA}$. CubeMX label: `D6_LED_HEATING`. Override BSP default (`ADC1_IN16` → `GPIO_Output`). ⚠️ **Verify**: Check `Analog → ADC1` in CubeMX left panel — `IN16` must not appear as an enabled channel after reassignment. |
| **PA4** | **CN3 pin 8** (D7) | `ARD.D7` | GPIO Output | Output Push-Pull, Initial Low | External Red LED (**Fault**) | Series Resistor: **330 Ω** to Anode. Cathode to GND. $V_f = 2.0\text{--}2.2\text{ V}$, $I \approx 3.6\text{--}3.9\text{ mA}$. CubeMX label: `D7_LED_FAULT`. Override BSP default (`ADC1_IN9` → `GPIO_Output`). ⚠️ **Verify**: Check `Analog → ADC1` and `Analog → DAC1` in CubeMX left panel — `IN9` and `DAC1_OUT1` must not appear as enabled after reassignment. |
| **PB10** | *Onboard* (I2C2) | `I2C2_SCL` | I2C2 | Alternate Function OD | Onboard VL53L0X ToF Distance Sensor | SCL line of I2C2 bus with 4.7 kΩ pull-up to 3.3V. **Cup Detection**. |
| **PB11** | *Onboard* (I2C2) | `I2C2_SDA` | I2C2 | Alternate Function OD | Onboard VL53L0X ToF Distance Sensor | SDA line of I2C2 bus with 4.7 kΩ pull-up to 3.3V. **Cup Detection**. |
| **PC6** | *Onboard* | `VL53L0X_XSHUT` | GPIO Output | Output Push-Pull, Initial **High** | Onboard VL53L0X Hardware Enable | Active-Low shutdown/reset. Driven HIGH at startup to enable sensor. CubeMX label: `VL53L0X_XSHUT` (rename from BSP default `VL53L0X XSHUT [Read XSHUT]` — remove spaces and brackets). |
| **PC7** | *Onboard* | `VL53L0X_GPIO1` | GPIO Input | Input Floating / INT | Onboard VL53L0X Interrupt Pin | Ranging data-ready interrupt (optional, polling supported). |
| **PE10** | *Onboard* (QSPI) | `QUADSPI_CLK` | QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Clock line (up to 80 MHz in High Performance mode). |
| **PE11** | *Onboard* (QSPI) | `QUADSPI_NCS` | QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Chip Select (Bank 1). |
| **PE12** | *Onboard* (QSPI) | `QUADSPI_BK1_IO0`| QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Data Line 0. |
| **PE13** | *Onboard* (QSPI) | `QUADSPI_BK1_IO1`| QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Data Line 1. |
| **PE14** | *Onboard* (QSPI) | `QUADSPI_BK1_IO2`| QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Data Line 2. |
| **PE15** | *Onboard* (QSPI) | `QUADSPI_BK1_IO3`| QUADSPI | Alternate Function | Onboard MX25R6435F 8 MB Flash | Quad-SPI Data Line 3. |
| **PB14** | *Onboard* | `LED2` | GPIO Output | Output Push-Pull | Onboard Green LED (LED2) | Onboard Heartbeat / Status indicator. |
| **PB6** | *Onboard* (VCP) | `STLK_TX` / USART1_TX | USART1 | Alternate Function PP | ST-LINK VCP UART Transmit | Virtual COM port communication to PC (115200 8N1). |
| **PB7** | *Onboard* (VCP) | `STLK_RX` / USART1_RX | USART1 | Alternate Function PP | ST-LINK VCP UART Receive | Virtual COM port communication from PC (115200 8N1). |

---

## 2. Power & Ground Distribution Header (CN2)

| Connector & Pin | Signal Name | Voltage Level | Destination on External Breadboard | Description |
| :--- | :--- | :---: | :--- | :--- |
| **CN2 pin 4** | `+3V3` | +3.3 V DC | **Red Positive Power Rail (+)** | Powers the high side of the 10 kΩ potentiometer. Max 500 mA from onboard LDO. |
| **CN2 pin 6** | `GND` | 0.0 V (Ground) | **Blue Negative Ground Rail (-)** | Common reference ground for all breadboard components. |
| **CN2 pin 7** | `GND` | 0.0 V (Ground) | *Alternative Common Ground* | Tied internally on PCB to CN2 pin 6. |

> [!IMPORTANT]
> **Common Ground Rule**: A single, solid ground connection must be established between CN2 pin 6 (or 7) and the breadboard blue ground rail. All breadboard component grounds (potentiometer low leg, pushbutton, selected strength-switch throw, and all LED cathodes) connect directly to this common rail. Leave the other strength-switch throw unconnected.

---

## 3. Electrical & Circuit Calculations

### 3.1 LED Current Limiting Resistors
The STM32L475 GPIO pins operate at $V_{DD} = 3.3\text{ V}$. Maximum rated current per I/O pin is $20\text{ mA}$, with recommended design current $\le 8\text{ mA}$ for high logic level integrity.

1. **Green Ready LED (PB4, CN3 pin 6)**:
   * Typical Forward Voltage ($V_f$): $3.0\text{ V}$ to $3.2\text{ V}$
   * Series Resistor ($R$): $150\ \Omega$ (5% tolerance, 1/4 W)
   * Voltage across resistor: $V_R = 3.3\text{ V} - V_f = 3.3\text{ V} - 3.1\text{ V (nom)} = 0.2\text{ V}$
   * Forward Current:
     $$I_{green} = \frac{V_{DD} - V_f}{R} = \frac{3.3\text{ V} - 3.1\text{ V}}{150\ \Omega} \approx 1.33\text{ mA} \quad (\text{range: } 0.67\text{ mA to } 2.00\text{ mA})$$
   * Result: Safe, efficient indication current with clear visibility and minimal power draw.

2. **Yellow Heating/Brewing LED (PB1, CN3 pin 7)**:
   * Typical Forward Voltage ($V_f$): $2.0\text{ V}$ to $2.2\text{ V}$
   * Series Resistor ($R$): $330\ \Omega$ (5% tolerance, 1/4 W)
   * Voltage across resistor: $V_R = 3.3\text{ V} - 2.1\text{ V (nom)} = 1.2\text{ V}$
   * Forward Current:
     $$I_{yellow} = \frac{V_{DD} - V_f}{R} = \frac{3.3\text{ V} - 2.1\text{ V}}{330\ \Omega} \approx 3.64\text{ mA} \quad (\text{range: } 3.33\text{ mA to } 3.94\text{ mA})$$
   * Result: Bright, well-regulated indication well below the $20\text{ mA}$ absolute maximum rating.

3. **Red Fault LED (PA4, CN3 pin 8)**:
   * Typical Forward Voltage ($V_f$): $2.0\text{ V}$ to $2.2\text{ V}$
   * Series Resistor ($R$): $330\ \Omega$ (5% tolerance, 1/4 W)
   * Voltage across resistor: $V_R = 3.3\text{ V} - 2.0\text{ V (nom)} = 1.3\text{ V}$
   * Forward Current:
     $$I_{red} = \frac{V_{DD} - V_f}{R} = \frac{3.3\text{ V} - 2.0\text{ V}}{330\ \Omega} \approx 3.94\text{ mA} \quad (\text{range: } 3.33\text{ mA to } 3.94\text{ mA})$$
   * Result: High contrast fault signal.

### 3.2 Total Current Consumption on 3.3V Rail
* Potentiometer divider ($10\text{ k}\Omega$): $I_{pot} = 3.3\text{ V} / 10\text{ k}\Omega = 0.33\text{ mA}$
* All 3 LEDs active simultaneously: $I_{LEDs} \approx 1.33 + 3.64 + 3.94 = 8.91\text{ mA}$
* Switches active (with internal ~40 kΩ pull-ups): $I_{switches} \approx 2 \times (3.3\text{ V} / 40\text{ k}\Omega) \approx 0.17\text{ mA}$
* **Total external circuit current**: $\approx 9.4\text{ mA}$ (fully compliant with B-L475E-IOT01A1 3.3V regulator capacity).

---

## 4. Physical Connector Orientation & Verification Checkpoints

> [!CAUTION]
> **Connector Verification Required Prior to Power-On**:
> 
> * **CN2 Header Pin Count & Indexing**: CN2 is an 8-position female socket header located along the lower-left edge of the B-L475E-IOT01A1 board. Pin 1 is adjacent to the board reset button. Pin 4 is silkscreened as `3V3` or `3.3V`, and Pins 6 and 7 are silkscreened as `GND`. **Verify silkscreen markings before inserting jumper wires.**
> * **CN4 Header (Analog A0–A5)**: CN4 is a 6-position female socket header located along the lower edge immediately to the right of CN2. Pin 1 corresponds to Arduino `A0` (`PC5`). Verify that Pin 1 is selected and not Pin 6 (`A5`).
> * **CN3 Header (Digital D0–D7)**: CN3 is an 8-position female socket header located along the upper-left edge of the board. Pin 1 is Arduino `D0` (adjacent to the micro-USB connector). Count sequentially:
>   * Pin 3: `D2` (`PD14`)
>   * Pin 5: `D4` (`PA3`)
>   * Pin 6: `D5` (`PB4`)
>   * Pin 7: `D6` (`PB1`)
>   * Pin 8: `D7` (`PA4`)
> * **Wire by Silkscreen Label**: Always connect jumper wires by matching the physical silkscreen label (`D2`, `D4`, `D5`, `D6`, `D7`, `A0`, `3V3`, `GND`) printed on the PCB rather than counting raw pin positions.
> * **PB4 Reset Glitch Note**: Pin `PB4` defaults to JTAG `NJTRST` at reset with an active internal pull-up. A brief sub-millisecond flash on the Ready LED may occur at power-up before GPIO initialization configures it low; this is normal hardware behavior.

---

## 5. Reserved Pins (Do Not Reassign or Connect to External Circuits)

| Subsystem | Reserved Pins | Hardware Destination & Dedicated Function |
| :--- | :--- | :--- |
| **Quad-SPI Flash** | `PE10`, `PE11`, `PE12`, `PE13`, `PE14`, `PE15` | Macronix MX25R6435F 8 MB NOR Flash (CLK, NCS, IO0–IO3). |
| **Debug Port (SWD)** | `PA13` (SWDIO), `PA14` (SWCLK) | Dedicated hardware debug interface to onboard ST-LINK. |
| **Cup Sensor Control**| `PC6` (XSHUT), `PC7` (GPIO1/INT) | VL53L0X hardware reset and interrupt lines. |
| **Wi-Fi Module** | `PC10`, `PC11`, `PC12`, `PB12`, `PE0`, `PE1`, `PE8` | Inventek ISM43362-M3G-L44 Wi-Fi module (SPI3 & control). Keep uninitialized. |
| **BLE Module** | `PD0`, `PD1`, `PA8`, `PB5`, `PA0` (shared) | SPBTLE-RF module (USART4 & control). Keep uninitialized. |
