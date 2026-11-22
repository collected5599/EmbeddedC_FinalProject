# Electrical Schematic — STM32 IoT Coffee Maker Controller

**Target Hardware**: B-L475E-IOT01A1 (STM32L475VGT6) & External Breadboard Subsystem  
**Operating Voltage ($V_{DD}$)**: 3.3 V DC  
**Reference Ground**: 0.0 V (Common Ground)  

---

## 1. Full Circuit Schematic Diagram

```text
                                        +3.3V (CN2 pin 4)
                                          |
                                          +---------------------------------------+
                                          |                                       |
                                         .-.                                     .-.
                                         | | R_POT                               | | R_PU (Internal ~40k)
                                         | | 10 kΩ Linear                        | | Inside STM32
                                         '-'                                     '-'
                                          |                                       |
            CN4 pin 1 (PC5 / ADC1_IN14) <-+ Wiper (0 to 3.3V)                     +-----> CN3 pin 3 (PD14 / EXTI14)
                                          |                                       |
                                          |                                       o
                                         === GND                                   \  SW1 (Pushbutton: Start/Cancel)
                                                                                  o
                                                                                  |
                                                                                 === GND
                                        +3.3V
                                          |
                                         .-.
                                         | | R_PU (Internal ~40k)
                                         | | Inside STM32
                                         '-'
                                          |
            CN3 pin 5 (PA3 / GPIO_IN) <--- COM
                                          \
                                           o---- Throw A ---- GND (CN2 pin 6)
                                           o---- Throw B ---- leave open
                                                SW2 (SPDT strength selector)


       ================================ OUTPUT CIRCUITS (LEDs) ================================

                                      R1 (150 Ω)        D1 (Green LED: Ready)
            CN3 pin 6 (PB4 / GPIO_OUT) --[====]--->|---.  Vf = 3.0V - 3.2V, If ≈ 1.3mA
                                                       |
                                                      === GND

                                      R2 (330 Ω)        D2 (Yellow LED: Heating/Brewing)
            CN3 pin 7 (PB1 / GPIO_OUT) --[====]--->|---.  Vf = 2.0V - 2.2V, If ≈ 3.6mA
                                                       |
                                                      === GND

                                      R3 (330 Ω)        D3 (Red LED: Fault)
            CN3 pin 8 (PA4 / GPIO_OUT) --[====]--->|---.  Vf = 2.0V - 2.2V, If ≈ 3.9mA
                                                       |
                                                      === GND


       ============================== COMMON GROUND INTERCONNECT ==============================

            STM32 Board (CN2 pin 6 / pin 7) --------------------------------+
                                                                            |
            Breadboard Common Ground Rail (GND) ============================+
                                                                            |
                                                                           === COMMON GND (0V)
```

---

## 2. Subsystem Schematics & Circuit Analysis

### 2.1 Analog Input Circuit: Water Temperature Simulation (PC5)
* **Pin**: PC5 (Connector CN4 pin 1, mapped to ADC1_IN14)
* **Circuit Configuration**: Voltage divider across the regulated 3.3 V supply.

```text
    +3.3V (CN2 pin 4) o------+
                             |
                            [ ] Top Element (0 to 10 kΩ)
                             |
    CN4 pin 1 (PC5)   o<-----+ Wiper
                             |
                            [ ] Bottom Element (10 kΩ to 0)
                             |
    GND (CN2 pin 6)   o------+
```

* **Circuit Equations**:
  $$V_{wiper} = 3.3\text{ V} \times \left( \frac{R_{bottom}}{R_{top} + R_{bottom}} \right)$$
  $$V_{wiper} \in [0.00\text{ V}, 3.30\text{ V}]$$
* **ADC Conversion (12-bit Resolution)**:
  $$\text{ADC\_RAW} = \frac{V_{wiper}}{3.3\text{ V}} \times 4095$$
  $$\text{Temperature}_{\text{sim}} = 20.0^\circ\text{C} + \left( \frac{\text{ADC\_RAW}}{4095} \times 90.0^\circ\text{C} \right) \implies [20.0^\circ\text{C} \text{ to } 110.0^\circ\text{C}]$$
* **Input Impedance & Filtering Consideration**: The ADC input sampling capacitor requires an analog source impedance $R_{AIN} \le 50\text{ k}\Omega$. A 10 kΩ potentiometer presents a maximum Thévenin output impedance of $R_{TH} = \frac{10\text{ k}\Omega}{4} = 2.5\text{ k}\Omega$ at mid-scale, well within STM32L4 specifications. An optional **100 nF ceramic capacitor** from wiper (PC5) to GND is recommended to filter breadboard contact noise and provide a charge reservoir for the ADC sampling switch.

---

### 2.2 Digital Input Circuit 1: Start / Cancel Pushbutton (PD14)
* **Pin**: PD14 (Connector CN3 pin 3, EXTI14 capable)
* **Configuration**: Active-low momentary switch using STM32 internal pull-up.

```text
    VDD (+3.3V Internal)
             |
            [ ] R_PU (Internal ~40 kΩ)
             |
    PD14 ----+----o   / o---- GND (CN2 pin 6)
                      SW1 (Pushbutton)
```

* **Electrical Characteristics**:
  * **Switch Open (Released)**: Pin voltage pulled up to $V_{DD} = 3.3\text{ V}$ (Logic High `1`). Leakage current $< 1\ \mu\text{A}$.
  * **Switch Closed (Pressed)**: Pin tied directly to GND (Logic Low `0`).
  * **Current during press**:
    $$I_{SW1} = \frac{3.3\text{ V}}{R_{PU}} \approx \frac{3.3\text{ V}}{40\text{ k}\Omega} \approx 0.0825\text{ mA} \quad (82.5\ \mu\text{A})$$
  * **Contact Debounce**: Managed in software via non-blocking periodic sampling (20–40 ms debounce window).

---

### 2.3 Digital Input Circuit 2: Three-Terminal Coffee Strength Switch (PA3)
* **Pin**: PA3 (Connector CN3 pin 5)
* **Switch**: Three-terminal, two-position SPDT toggle switch.
* **Configuration**: PA3 connects to COM. One outer throw connects to GND; leave the other outer throw unconnected. Use a continuity meter to identify COM and the throws before wiring.

```text
    VDD (+3.3V Internal)
             |
            [ ] R_PU (Internal ~40 kΩ)
             |
    PA3 ----- COM o
                  \
                   o---- Throw A ---- GND (CN2 pin 6)
                   o---- Throw B ---- no connection
                      SW2 (SPDT Strength Switch)
```

* **Logic Mapping**:
  * **COM touching grounded throw (Strong Mode)**: PA3 = $0.0\text{ V}$ (Logic Low `0`).
  * **COM touching unused throw (Normal Mode)**: PA3 = $3.3\text{ V}$ (Logic High `1`) through the STM32 internal pull-up.
  * If the physical lever direction is opposite the desired mode, move the GND wire to the other outer throw.
  * **Current consumption**: Zero in the Normal position; approximately $82.5\ \mu\text{A}$ in the grounded Strong position.

---

### 2.4 Digital Input Circuit 3: Onboard Power Button (PC13)
* **Pin**: PC13 (Onboard User Pushbutton B2, net `BUTTON_EXTI13`; Note: B1 is NRST Reset)
* **Configuration**: Hardwired on B-L475E-IOT01A1 PCB.
  * External $10\text{ k}\Omega$ pull-up resistor to 3.3 V on PCB.
  * Debouncing capacitor $100\text{ nF}$ to ground on PCB.
  * Pushbutton pulls PC13 directly to GND when pressed.
  * **Logic**: Idle = High (`1`), Pressed = Low (`0`).

---

### 2.5 Digital Output Circuits: Status Indicator LEDs

#### 1. Green Ready LED (PB4 / CN3 pin 6)
* **Current-Limiting Resistor ($R1$)**: $150\ \Omega$, 1/4 W
* **Forward Voltage ($V_f$)**: $3.0\text{ V}$ to $3.2\text{ V}$ (Nominal $3.1\text{ V}$)
* **Drive Voltage ($V_{OH}$)**: $\approx 3.3\text{ V}$
* **Calculations**:
  $$V_{R1} = V_{OH} - V_f = 3.3\text{ V} - 3.1\text{ V} = 0.2\text{ V}$$
  $$I_{LED1} = \frac{V_{R1}}{R1} = \frac{0.2\text{ V}}{150\ \Omega} \approx 1.33\text{ mA}$$
  $$P_{R1} = I_{LED1}^2 \times R1 = (1.33\text{ mA})^2 \times 150\ \Omega \approx 0.27\text{ mW} \ll 250\text{ mW}$$

#### 2. Yellow Heating/Brewing LED (PB1 / CN3 pin 7)
* **Current-Limiting Resistor ($R2$)**: $330\ \Omega$, 1/4 W
* **Forward Voltage ($V_f$)**: $2.0\text{ V}$ to $2.2\text{ V}$ (Nominal $2.1\text{ V}$)
* **Calculations**:
  $$V_{R2} = V_{OH} - V_f = 3.3\text{ V} - 2.1\text{ V} = 1.2\text{ V}$$
  $$I_{LED2} = \frac{V_{R2}}{R2} = \frac{1.2\text{ V}}{330\ \Omega} \approx 3.64\text{ mA}$$
  $$P_{R2} = (3.64\text{ mA})^2 \times 330\ \Omega \approx 4.37\text{ mW} \ll 250\text{ mW}$$

#### 3. Red Fault LED (PA4 / CN3 pin 8)
* **Current-Limiting Resistor ($R3$)**: $330\ \Omega$, 1/4 W
* **Forward Voltage ($V_f$)**: $2.0\text{ V}$ to $2.2\text{ V}$ (Nominal $2.0\text{ V}$)
* **Calculations**:
  $$V_{R3} = V_{OH} - V_f = 3.3\text{ V} - 2.0\text{ V} = 1.3\text{ V}$$
  $$I_{LED3} = \frac{V_{R3}}{R3} = \frac{1.3\text{ V}}{330\ \Omega} \approx 3.94\text{ mA}$$
  $$P_{R3} = (3.94\text{ mA})^2 \times 330\ \Omega \approx 5.12\text{ mW} \ll 250\text{ mW}$$

---

## 3. Netlist Summary

| Net Name | Connection Origin | Connection Destination | Signal Description |
| :--- | :--- | :--- | :--- |
| `+3V3` | STM32 CN2 pin 4 | Potentiometer Pin 1 | Main +3.3 V DC system power |
| `GND` | STM32 CN2 pin 6 / 7 | Breadboard Common GND rail | Common 0.0 V system reference |
| `NET_ADC_TEMP` | Potentiometer Pin 2 (Wiper)| STM32 CN4 pin 1 (PC5) | Simulated temperature analog voltage |
| `NET_BTN_START`| Breadboard Pushbutton Leg 1| STM32 CN3 pin 3 (PD14) | Start/Cancel momentary switch line |
| `NET_SW_STRONG`| SPDT switch COM; selected outer throw to GND | STM32 CN3 pin 5 (PA3) | Strength mode selector; unused throw left open |
| `NET_LED_READY`| STM32 CN3 pin 6 (PB4) | 150 Ω Resistor -> Green LED Anode | Ready state indicator drive |
| `NET_LED_HEAT` | STM32 CN3 pin 7 (PB1) | 330 Ω Resistor -> Yellow LED Anode| Heating/Brewing state indicator drive |
| `NET_LED_FAULT`| STM32 CN3 pin 8 (PA4) | 330 Ω Resistor -> Red LED Anode | Fault state indicator drive |
| `NET_I2C2_SCL` | STM32 Internal (PB10) | Onboard VL53L0X Pin 5 (SCL) | I2C Clock (Cup Sensor) |
| `NET_I2C2_SDA` | STM32 Internal (PB11) | Onboard VL53L0X Pin 6 (SDA) | I2C Data (Cup Sensor) |
| `NET_VCP_TX` | STM32 Internal (PB6) | Onboard ST-LINK VCP RX | Virtual COM Port Transmit |
| `NET_VCP_RX` | STM32 Internal (PB7) | Onboard ST-LINK VCP TX | Virtual COM Port Receive |
