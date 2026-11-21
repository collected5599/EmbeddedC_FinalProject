# System Block Diagram — STM32 IoT Coffee Maker

This document defines the high-level system architecture, hardware-software boundaries, and subsystem interconnects between the **STM32L475VG Microcontroller**, physical external breadboard interfaces, onboard sensors, and the **PC Python Digital Twin**.

---

## 1. Complete Architecture Diagram

```mermaid
flowchart TB
    subgraph External_Breadboard ["External Breadboard Subsystem"]
        POT["10 kΩ Potentiometer<br/>(Simulated Water Temp)<br/>3.3V <--> GND"]
        BTN_EXT["External Pushbutton<br/>(Start / Cancel)<br/>Active-Low to GND"]
        SW_LEV["Lever Switch<br/>(Normal / Strong)<br/>Active-Low to GND"]
        
        LED_GRN["Green LED (Ready)<br/>Vf = 3.0-3.2V<br/>Series Resistor: 150 Ω"]
        LED_YEL["Yellow LED (Heat/Brew)<br/>Vf = 2.0-2.2V<br/>Series Resistor: 330 Ω"]
        LED_RED["Red LED (Fault)<br/>Vf = 2.0-2.2V<br/>Series Resistor: 330 Ω"]
        
        GND_RAIL["Common Ground Rail<br/>(0.0V Reference)"]
    end

    subgraph STM32_Board ["B-L475E-IOT01A1 Development Board"]
        subgraph Physical_Headers ["Board Expansion Headers"]
            CN2_PWR["CN2 Power Header<br/>Pin 4: +3.3V<br/>Pin 6/7: GND"]
            CN4_ANA["CN4 Analog Header<br/>Pin 1: PC5 (ADC1_IN14)"]
            CN3_DIG["CN3 Digital Header<br/>Pin 3: PD14 (EXTI14)<br/>Pin 5: PA3 (GPIO_IN)<br/>Pin 6: PB4 (GPIO_OUT)<br/>Pin 7: PB1 (GPIO_OUT)<br/>Pin 8: PA4 (GPIO_OUT)"]
        end

        subgraph Onboard_HW ["Onboard Hardware & Sensors"]
            BTN_ONB["User Button B2<br/>(PC13 - BUTTON_EXTI13)"]
            LED_ONB["User LED2 (PB14)<br/>Heartbeat / Status"]
            TOF_SENS["VL53L0X ToF Distance Sensor<br/>(Cup Detection)<br/>I2C2: PB10 (SCL) / PB11 (SDA)<br/>Enable: PC6 (XSHUT)"]
            QSPI_FLASH["Macronix MX25R6435F 8 MB Flash<br/>(Settings & Event Logs)<br/>QUADSPI: PE10-PE15"]
            STLINK["ST-LINK / V2-1 VCP<br/>USART1: PB6 (TX) / PB7 (RX)"]
        end

        subgraph STM32_Firmware ["Bare-Metal Embedded Firmware (C / HAL)"]
            F_BM["button_manager<br/>(Debounce, Edges)"]
            F_TS["temp_sensor<br/>(ADC Filter & Scaling)"]
            F_CS["cup_sensor<br/>(VL53L0X I2C Ranging)"]
            F_STORAGE["storage_manager & qspi_flash<br/>(Ping-Pong Config & Circular Log)"]
            F_SM["coffee_sm<br/>(IDLE -> HEAT -> BREW)"]
            F_FM["fault_manager<br/>(Safety & Interlocks)"]
            F_LM["led_manager<br/>(Non-blocking Patterns)"]
            F_UP["uart_protocol<br/>(Packet Framing & Telemetry)"]
        end
    end

    subgraph Host_PC ["Host PC Application"]
        subgraph Python_Twin ["Python Digital Twin Simulation"]
            PY_COMM["Serial Communication Manager<br/>(PySerial @ 115200 8N1)"]
            PY_TANK["Water Tank & Thermal Model<br/>(Heating, Cooling, Volume)"]
            PY_ACT["Virtual Actuators<br/>(Simulated Heater & Water Pump)"]
            PY_UI["Console / Graphical Dashboard<br/>(Status, Telemetry, Controls)"]
        end
    end

    %% Power Distribution
    CN2_PWR -->|"+3.3V Power"| POT
    CN2_PWR -->|"Common GND"| GND_RAIL
    GND_RAIL --> POT
    GND_RAIL --> BTN_EXT
    GND_RAIL --> SW_LEV
    GND_RAIL --> LED_GRN
    GND_RAIL --> LED_YEL
    GND_RAIL --> LED_RED

    %% Physical Inputs to Headers
    POT -->|"Wiper Voltage (0-3.3V)"| CN4_ANA
    BTN_EXT -->|"Active-Low Signal"| CN3_DIG
    SW_LEV -->|"Active-Low Signal"| CN3_DIG

    %% Headers to Firmware Inputs
    CN4_ANA --> F_TS
    CN3_DIG --> F_BM
    BTN_ONB --> F_BM
    TOF_SENS --> F_CS
    QSPI_FLASH <--> F_STORAGE

    %% Firmware Dataflow
    F_BM --> F_SM
    F_TS --> F_SM
    F_CS --> F_SM
    F_TS --> F_FM
    F_CS --> F_FM
    F_SM <--> F_FM
    F_SM <--> F_STORAGE
    F_FM <--> F_STORAGE
    F_STORAGE <--> F_UP
    F_SM --> F_LM
    F_FM --> F_LM
    F_SM <--> F_UP
    F_FM --> F_UP

    %% Firmware Outputs to Headers & Hardware
    F_LM --> CN3_DIG
    F_LM --> LED_ONB
    CN3_DIG -->|"Drive Signal"| LED_GRN
    CN3_DIG -->|"Drive Signal"| LED_YEL
    CN3_DIG -->|"Drive Signal"| LED_RED

    %% Communication Interface
    F_UP <--> STLINK
    STLINK <== USB Serial Link (115200 baud) ==> PY_COMM
    PY_COMM <--> PY_TANK
    PY_COMM <--> PY_ACT
    PY_COMM <--> PY_UI
    PY_TANK <--> PY_ACT
    PY_ACT <--> PY_UI
```

---

## 2. Subsystem Interface Breakdown

### 2.1 Sensor & Input Subsystem
* **Potentiometer (PC5 / ADC1_IN14)**:
  * Analog input range: $0.0\text{ V} \le V_{in} \le 3.3\text{ V}$.
  * Sampled periodically by `temp_sensor` using non-blocking ADC conversions.
  * Scaled to representation of boiler water temperature ($20^\circ\text{C}$ to $110^\circ\text{C}$).
* **User Buttons & Switches**:
  * **PC13 (Onboard User Button B2, BUTTON_EXTI13)**: System Power Toggle (Standby vs Active). Note: B1 is NRST Reset button.
  * **PD14 (External Pushbutton)**: Brew Start / Abort command.
  * **PA3 (External Lever Switch)**: Coffee profile selector:
    * Logic High (`1`): Normal Brew (Standard water volume & duration).
    * Logic Low (`0`): Strong Brew (Extended pre-infusion & extraction profile).
* **Cup Presence Sensor (VL53L0X)**:
  * Communicates via I2C2 (`PB10` SCL / `PB11` SDA) at $100\text{ kHz}$.
  * Measures distance to platform in millimeters.
  * Hysteresis qualification: Cup Present ($d < 80\text{ mm}$), Cup Absent ($d \ge 100\text{ mm}$).

---

### 2.2 Actuator & Output Subsystem
* **Physical Status Indicators (External LEDs)**:
  * **Ready LED (PB4 / 150 Ω)**: Solid Green indicates system is powered, water is heated to target temperature, and cup is detected.
  * **Heating / Brewing LED (PB1 / 330 Ω)**:
    * Pulsing/Blinking (1 Hz): Water is heating to target temperature.
    * Solid Yellow: Pump active, actively dispensing coffee into cup.
  * **Fault LED (PA4 / 330 Ω)**: Fast Blinking (4 Hz) or Solid Red indicating an interlock violation (e.g., cup removed mid-brew, thermal fault, water reservoir empty).
* **Onboard Heartbeat LED (PB14)**:
  * Toggles every 500 ms to confirm non-blocking superloop health.

---

### 2.3 PC Python Digital Twin Subsystem
* **Physical Link**: Full-speed USB virtual serial link through ST-LINK VCP on `PB6` (TX) and `PB7` (RX) at **115,200 baud, 8 data bits, no parity, 1 stop bit (8N1)**.
* **Role Separation**:
  * **STM32 Controller**: Owns all safety logic, state transitions, hardware reading, debouncing, and local LED indication.
  * **Python Digital Twin**:
    * Simulates high-voltage actuators (Heater heating element and 15-bar water pump).
    * Simulates physical plant thermodynamics (thermal loss, boiler heating rate based on wattage).
    * Simulates water reservoir level and consumption per brew.
    * Provides user monitoring interface and fault injection for automated validation.
