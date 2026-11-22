from html import escape
from pathlib import Path


OUT = Path(__file__).resolve().parent
W, H = 1600, 900
TOTAL_STEPS = 14

SLIDES = [
    {
        "slug": "01_POWER_STANDBY", "title": "1. Power on → standby", "state": "STANDBY",
        "subtitle": "The controller boots safely and waits for the power button.", "cup": 0, "cup_visible": False, "stream": False,
        "action": "Connect USB power. No button press is needed yet.",
        "logic": "MCU starts sensors and status reporting. Coffee cycle stays off.",
        "leds": [("READY · PB4", "OFF", "off"), ("HEATING · PB1", "OFF", "off"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "HEATER OFF     PUMP OFF", "callout": "Next: press the onboard user button (PC13).",
        "chips": ["PC13 · POWER", "PD14 · START", "PA3 · STRENGTH", "PC5 · TEMP POT", "VL53L0X · CUP"],
    },
    {
        "slug": "02_IDLE_CUP", "title": "2. Wake up and check for a cup", "state": "IDLE", "subtitle": "The controller is ready for a brew request and keeps checking the cup sensor.", "cup": 0, "stream": False,
        "cup_visible": True, "action": "Press the onboard user button (PC13). Place a mug under the spout.",
        "logic": "Onboard VL53L0X reports cup distance over I2C2. Present <80 mm; absent ≥100 mm; 80–99 mm keeps the prior result.",
        "leds": [("READY · PB4", "OFF until cup + target", "off"), ("HEATING · PB1", "OFF", "off"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "HEATER OFF     PUMP OFF", "callout": "The ready light stays off until a valid cup and target temperature are both present.",
        "chips": ["PC13 · ONBOARD USER BUTTON", "VL53L0X · I2C2", "PC5 · 20–110 °C SIM INPUT"],
    },
    {
        "slug": "03_START_CHECK", "title": "3. Select strength and request a brew", "state": "IDLE", "subtitle": "The start request is accepted only when the ToF sensor has a valid cup reading.", "cup": 0, "stream": False,
        "cup_visible": False, "action": "Set PA3 to Normal or Strong, then press PD14 Start.",
        "logic": "No valid cup? The request is ignored. With a cup, target is 92 °C Normal or 95 °C Strong. Below target enters HEATING; already warm enough goes straight to BREWING.",
        "leds": [("READY · PB4", "OFF until cup + target", "off"), ("HEATING · PB1", "OFF in IDLE", "off"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "NO CUP → REQUEST IGNORED", "callout": "Try this safety check: press Start with no cup and confirm the state stays IDLE.",
        "chips": ["PA3 · NORMAL / STRONG", "PD14 · START / CANCEL", "CUP MUST BE VALID"],
    },
    {
        "slug": "04_HEATING", "title": "4. Heat to the selected target", "state": "HEATING", "subtitle": "This step appears when the simulated temperature starts below the selected target.", "cup": 0, "stream": False,
        "cup_visible": True, "action": "Turn the PC5 potentiometer to raise the simulated temperature.",
        "logic": "Temperature input maps to 20–110 °C. The controller requests 100% virtual heater power until 92 °C Normal or 95 °C Strong.",
        "leds": [("READY · PB4", "OFF", "off"), ("HEATING · PB1", "BLINK 1 Hz", "yellow"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "HEATER 100% VIRTUAL     PUMP OFF", "callout": "Cup is monitored during heating. Removing it for ≥200 ms causes NO_CUP fault.",
        "chips": ["PC5 · ADC1_IN14", "TARGET · 92 / 95 °C", "PUMP · OFF"],
    },
    {
        "slug": "05_PREINFUSION", "title": "5. Brew begins: pre-infusion", "state": "BREWING", "subtitle": "At target temperature, the virtual pump starts and the mug begins to fill.", "cup": 22, "stream": True,
        "cup_visible": True, "action": "Keep the mug in place. Watch the pump timer start.",
        "logic": "Pre-infusion runs 2 s Normal or 5 s Strong. The pump is ON; the heater request remains OFF in BREWING.",
        "leds": [("READY · PB4", "OFF", "off"), ("HEATING · PB1", "SOLID ON", "yellow"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "PUMP RUNNING · TIMER COUNTS UP", "callout": "Expected pump-active total: 20 s Normal or 32 s Strong. Heater and pump are software simulations.",
        "chips": ["NORMAL · 2 s PRE-INFUSION", "STRONG · 5 s PRE-INFUSION", "MUG · STARTING TO FILL"],
    },
    {
        "slug": "06_BLOOM_EXTRACTION", "title": "6. Pause, then extract coffee", "state": "BREWING", "subtitle": "A short bloom pause stops the pump timer; extraction then resumes and fills the mug.", "cup": 68, "stream": True,
        "cup_visible": True, "action": "Observe the 2 s pause, then the pump resumes.",
        "logic": "During the 2 s bloom pause the pump is OFF and its active-time timer holds. The heater request remains OFF in BREWING; extraction resumes to 20 s Normal or 32 s Strong pump-on total.",
        "leds": [("READY · PB4", "OFF", "off"), ("HEATING · PB1", "SOLID ON", "yellow"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "BLOOM: PUMP PAUSED 2 s  →  EXTRACTION: ON", "callout": "The virtual mug fill follows accumulated pump ON-time; it holds during the bloom pause.",
        "chips": ["2 s · BLOOM PAUSE", "TIMER · PAUSES WITH PUMP", "VIRTUAL MUG · FILLING"],
    },
    {
        "slug": "07_COMPLETE", "title": "7. Brew complete", "state": "COMPLETE", "subtitle": "The pump and heater stop; the finished mug can be removed.", "cup": 100, "stream": False,
        "cup_visible": True, "action": "Remove the mug, or wait for the 5 s completion timeout.",
        "logic": "COMPLETE ends when the cup is removed or after 5 s, then the controller returns to IDLE.",
        "leds": [("READY · PB4", "BLINK 1 Hz", "green"), ("HEATING · PB1", "BLINK 1 Hz", "yellow"), ("FAULT · PA4", "OFF", "off"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "HEATER OFF     PUMP OFF", "callout": "The mug graphic is a visual estimate based on pump time, not a liquid-level sensor.",
        "chips": ["BREW DONE", "REMOVE CUP → IDLE", "OR WAIT 5 s → IDLE"],
    },
    {
        "slug": "08_FAULT_RECOVERY", "title": "8. Fault response and recovery", "state": "FAULT", "subtitle": "Safety faults stop the simulated actuators and make the cause visible.", "cup": 0, "stream": False,
        "cup_visible": False, "action": "Correct the cause, then press PD14 when the fault condition is clear.",
        "logic": "Faults: cup absent ≥200 ms while heating/brewing; temperature >102 °C; heating >180 s; sensor communication error.",
        "leds": [("READY · PB4", "OFF", "off"), ("HEATING · PB1", "OFF", "off"), ("FAULT · PA4", "BLINK 4 Hz", "red"), ("HEARTBEAT · PB14", "BLINK 1 Hz", "green")],
        "actuator": "HEATER OFF     PUMP OFF", "callout": "Press PD14 to clear only when safe. PC13 returns to STANDBY. Tank-empty sensing is not installed.",
        "chips": ["NO_CUP · 200 ms", "OVERTEMP · >102 °C", "HEAT TIMEOUT · 180 s", "SENSOR ERROR"],
    },
]


def text(x, y, value, size=22, color="#e9f0f2", weight=400, anchor="start"):
    return f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-family="Segoe UI,Arial,sans-serif" font-size="{size}" font-weight="{weight}" fill="{color}">{escape(value)}</text>'


def machine(slide):
    # Appliance illustration; its four lamps mirror the board outputs listed at right.
    x, y = 255, 266
    cup = slide["cup"]
    coffee_top = 699 - 0.79 * cup
    s = [f'<rect x="{x}" y="{y}" width="430" height="395" rx="32" fill="#e7eeec" stroke="#ffffff" stroke-width="4"/>',
         f'<rect x="{x+24}" y="{y+24}" width="382" height="110" rx="20" fill="#263b46"/>',
         f'<rect x="{x+40}" y="{y+40}" width="82" height="77" rx="10" fill="#142b36"/>',
         text(x+81, y+68, "SIM", 15, "#97b3bd", 700, "middle"),
         f'<rect x="{x+49}" y="{y+81}" width="64" height="27" rx="5" fill="#248899"/>']
    led_colors = {"off": "#455761", "green": "#55d887", "yellow": "#f4c657", "red": "#ff6a70"}
    for i, color in enumerate(("green", "yellow", "red", "green")):
        state = slide["leds"][i][2]
        lit = led_colors[state]
        # Heartbeat always has an active pattern; blink is explained in the LED card.
        s.append(f'<circle cx="{x+170+i*62}" cy="{y+70}" r="15" fill="{lit}" stroke="#8da0a7" stroke-width="2"/>')
    s += [text(x+285, y+118, slide["state"], 17, "#f1f6f6", 700, "middle"),
          f'<rect x="{x+112}" y="{y+155}" width="206" height="42" rx="9" fill="#aebdc0"/>',
          f'<rect x="{x+182}" y="{y+197}" width="66" height="102" fill="#2d424b"/>',
          f'<rect x="{x+157}" y="{y+299}" width="116" height="15" rx="6" fill="#172a33"/>']
    if slide["cup_visible"]:
        s.append(f'<path d="M{x+132} 620 L{x+298} 620 L{x+284} 705 Q{x+215} 733 {x+146} 705 Z" fill="#f7f8f3" stroke="#b9c7c7" stroke-width="4"/>')
        s.append(f'<path d="M{x+151} {coffee_top:.1f} L{x+280} {coffee_top:.1f} L{x+279} 699 Q{x+214} 720 {x+150} 699 Z" fill="#774b32"/>')
        s.append(f'<path d="M{x+298} 639 Q{x+340} 632 {x+338} 662 Q{x+337} 690 {x+286} 679" fill="none" stroke="#c0cdcd" stroke-width="9"/>')
    if slide["stream"]:
        s.append(f'<path d="M{x+215} {y+314} L{x+215} 607" stroke="#a96b3f" stroke-width="8" stroke-linecap="round" stroke-dasharray="16 11"/>')
    s += [text(x+212, 758, "VIRTUAL MUG", 17, "#8fa8b3", 700, "middle"),
          text(x+212, 785, f'{cup}% fill illustration' if slide["cup_visible"] else "NO CUP", 17, "#c7d4d9", 500, "middle")]
    return "\n".join(s)


def render(slide, idx):
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
             '<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#14212c"/><stop offset="1" stop-color="#0b131b"/></linearGradient><filter id="shadow" x="-10%" y="-10%" width="120%" height="125%"><feDropShadow dx="0" dy="9" stdDeviation="9" flood-color="#000" flood-opacity=".24"/></filter></defs>',
             '<rect width="1600" height="900" fill="url(#bg)"/>',
             text(58, 70, "COFFEE MAKER · STEP-BY-STEP", 18, "#62d7c6", 700),
             text(58, 126, slide["title"], 43, "#f3f7f8", 700),
             text(60, 164, slide["subtitle"], 22, "#aec0c8", 400),
             '<rect x="55" y="194" width="720" height="600" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
             '<rect x="815" y="194" width="730" height="600" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
             text(90, 239, "MACHINE VIEW", 17, "#8fa8b3", 700),
             text(850, 239, "WHAT TO DO AND WHAT TO EXPECT", 17, "#8fa8b3", 700),
             machine(slide),
             '<line x1="850" y1="263" x2="1510" y2="263" stroke="#304754" stroke-width="2"/>',
             text(850, 300, "YOUR ACTION", 17, "#62d7c6", 700),
             text(850, 337, slide["action"], 22, "#edf3f4", 500),
             text(850, 390, "CONTROLLER RESPONSE", 17, "#62d7c6", 700)]
    logic_lines = wrap(slide["logic"], 53)
    for j, line in enumerate(logic_lines):
        parts.append(text(850, 426+j*29, line, 20, "#c5d3d9", 400))
    led_heading_y = max(519, 426+len(logic_lines)*29+20)
    parts.append(text(850, led_heading_y, "BOARD INDICATORS", 17, "#62d7c6", 700))
    colors = {"off": "#8798a1", "green": "#55d887", "yellow": "#f4c657", "red": "#ff6a70"}
    for j, (name, status, color) in enumerate(slide["leds"]):
        yy = led_heading_y + 34 + j*37
        parts += [f'<circle cx="{864}" cy="{yy-6}" r="9" fill="{colors[color]}"/>',
                  text(885, yy, name, 18, "#e7eef0", 650),
                  text(1505, yy, status, 18, "#c4d2d8", 600, "end")]
    parts += ['<rect x="850" y="724" width="660" height="44" rx="12" fill="#203945"/>',
              text(870, 753, slide["actuator"], 18, "#e1ecee", 700),
              text(58, 832, slide["callout"], 19, "#e3e9eb", 500),
              text(1542, 832, f'{idx:02d} / {TOTAL_STEPS:02d}', 17, "#9db1bb", 700, "end"),
              '<rect x="58" y="856" width="1484" height="4" rx="2" fill="#263a46"/>',
              f'<rect x="58" y="856" width="{1484*idx/TOTAL_STEPS:.1f}" height="4" rx="2" fill="#58d6c6"/>',
              text(58, 886, "Expected firmware behavior · coffee mode (IO_DIAGNOSTICS_MODE=0) · hardware validation pending · heater/pump are virtual", 14, "#80959f", 500),
              '</svg>']
    return "\n".join(parts)


def wrap(value, width):
    words, lines, current = value.split(), [], ""
    for word in words:
        if len(current) + len(word) + (1 if current else 0) > width:
            lines.append(current)
            current = word
        else:
            current = f"{current} {word}" if current else word
    if current:
        lines.append(current)
    return lines


for i, slide in enumerate(SLIDES, start=1):
    (OUT / f'COFFEE_MAKER_STEP_{slide["slug"]}.svg').write_text(render(slide, i), encoding="utf-8")


def storage_slide():
    a = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
         '<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#14212c"/><stop offset="1" stop-color="#0b131b"/></linearGradient><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="10" markerHeight="10" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="#58d6c6"/></marker></defs>',
         '<rect width="1600" height="900" fill="url(#bg)"/>',
         text(58, 70, "COFFEE MAKER · PROJECT DATA", 18, "#62d7c6", 700),
         text(58, 126, "9. What the QSPI flash stores", 43, "#f3f7f8", 700),
         text(60, 164, "The onboard serial flash keeps a compact history of important controller events.", 22, "#aec0c8"),
         '<rect x="58" y="202" width="705" height="560" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
         '<rect x="795" y="202" width="747" height="560" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
         text(92, 248, "EVENT RECORD · 32 BYTES", 17, "#8fa8b3", 700),
         '<rect x="92" y="274" width="638" height="306" rx="18" fill="#203744" stroke="#355260" stroke-width="2"/>']
    fields = [
        ("WHEN", "Sequence number · uptime tick"),
        ("WHY", "Boot, state change, fault event"),
        ("CONDITION", "State · fault · Normal / Strong"),
        ("SENSORS", "Temperature · valid cup distance"),
        ("OUTPUT REQUESTS", "Heater percent · pump on/off"),
        ("CHECK", "CRC16 integrity value"),
    ]
    for n, (heading, desc) in enumerate(fields):
        yy = 314 + n*43
        a += [text(118, yy, heading, 16, "#62d7c6", 700), text(338, yy, desc, 19, "#e7eef0", 500)]
    a += ['<rect x="92" y="606" width="638" height="128" rx="16" fill="#14222c"/>',
          text(120, 638, "LOCATION", 16, "#62d7c6", 700),
          text(120, 670, "Sectors 1–2 · alternating ToF profiles", 18, "#edf3f4", 600),
          text(120, 698, "Sectors 3–15 · circular event log", 18, "#edf3f4", 600),
          text(120, 722, "Sector 0 is untouched.", 15, "#aebfc7", 400),
          text(830, 248, "HOW DATA MOVES", 17, "#8fa8b3", 700),
          '<rect x="835" y="280" width="192" height="130" rx="18" fill="#203744" stroke="#355260" stroke-width="2"/>',
          text(931, 320, "1 · EVENT", 18, "#62d7c6", 700, "middle"),
          text(931, 355, "Boot / state / fault", 16, "#e7eef0", 500, "middle"),
          '<rect x="1080" y="280" width="192" height="130" rx="18" fill="#203744" stroke="#355260" stroke-width="2"/>',
          text(1176, 320, "2 · RECORD", 18, "#62d7c6", 700, "middle"),
          text(1176, 355, "CRC + values", 16, "#e7eef0", 500, "middle"),
          '<rect x="1325" y="280" width="185" height="130" rx="18" fill="#203744" stroke="#355260" stroke-width="2"/>',
          text(1417, 320, "3 · QSPI", 18, "#62d7c6", 700, "middle"),
          text(1417, 355, "Write + verify", 16, "#e7eef0", 500, "middle"),
          '<path d="M1028 345 L1072 345 M1273 345 L1317 345" stroke="#58d6c6" stroke-width="4" marker-end="url(#arr)"/>',
          '<rect x="835" y="444" width="675" height="114" rx="18" fill="#17372f" stroke="#36725d" stroke-width="2"/>',
          text(865, 481, "AVAILABLE NOW", 16, "#64dfa1", 700),
          text(865, 507, "Events are appended; startup scans existing records.", 17, "#e7eef0", 500),
          text(865, 533, "Each write is read-back checked.", 17, "#e7eef0", 500),
          '<rect x="835" y="580" width="675" height="138" rx="18" fill="#392d20" stroke="#8a6737" stroke-width="2"/>',
          text(865, 617, "LOG RETRIEVAL · IMPLEMENTED", 16, "#f1c66f", 700),
          text(865, 654, "GUI sends CMD,READ_LOGS over USART1; STM32 returns CRC-valid QSPI events.", 17, "#f0e9dc", 500),
          text(865, 687, "View in the log table or save CSV. This command does not alter flash.", 17, "#f0e9dc", 500),
          text(58, 811, "Sectors 1–2 store calibrated cup thresholds; Read QSPI log downloads event history.", 19, "#e3e9eb", 500),
          text(58, 856, "Read command and CSV export are implemented in software · hardware retrieval still needs validation", 14, "#80959f", 500),
          text(1542, 811, "09 / 14", 17, "#9db1bb", 700, "end"),
          '</svg>']
    return "\n".join(a)


def gui_slide():
    a = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
         '<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#14212c"/><stop offset="1" stop-color="#0b131b"/></linearGradient><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="9" markerHeight="9" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="#58d6c6"/></marker></defs>',
         '<rect width="1600" height="900" fill="url(#bg)"/>',
         text(58, 70, "COFFEE MAKER · PYTHON DIGITAL TWIN", 18, "#62d7c6", 700),
         text(58, 126, "10. The GUI at a glance", 43, "#f3f7f8", 700),
         text(60, 164, "A labeled dashboard preview: live controller values drive the machine view and indicators.", 22, "#aec0c8"),
         '<rect x="58" y="202" width="1080" height="590" rx="20" fill="#101923" stroke="#44535f" stroke-width="3"/>',
         '<rect x="72" y="216" width="1052" height="52" rx="12" fill="#101923"/>',
         text(95, 250, "COFFEE MAKER", 23, "#edf4f7", 700),
         '<rect x="851" y="229" width="246" height="30" rx="14" fill="#17372f"/>',
         text(974, 250, "SAMPLE · LIVE STATUS VIEW", 14, "#64dfa1", 700, "middle"),
         '<rect x="72" y="279" width="1052" height="48" rx="10" fill="#192633"/>',
         text(92, 309, "COM PORT   COMx", 14, "#9aadb9", 600),
         '<rect x="292" y="288" width="98" height="30" rx="6" fill="#54d6c5"/>',
         text(341, 309, "CONNECT", 13, "#0b2024", 700, "middle"),
         '<rect x="408" y="288" width="132" height="30" rx="6" fill="#223342" stroke="#54d6c5" stroke-width="1"/>',
         text(474, 308, "READ QSPI LOG", 11, "#54d6c5", 700, "middle"),
         text(1090, 309, "115200 8N1", 13, "#9aadb9", 600, "end"),
         '<rect x="72" y="340" width="560" height="432" rx="14" fill="#192633"/>',
         text(96, 373, "LIVE MACHINE MODEL", 13, "#9aadb9", 700),
         '<rect x="230" y="400" width="252" height="260" rx="18" fill="#dbe4e5" stroke="#f4f7f5" stroke-width="2"/>',
         '<rect x="248" y="416" width="216" height="61" rx="10" fill="#273945"/>',
         '<circle cx="330" cy="440" r="9" fill="#455761"/><circle cx="362" cy="440" r="9" fill="#f0c15a"/><circle cx="394" cy="440" r="9" fill="#455761"/><circle cx="426" cy="440" r="9" fill="#54d6c5"/>',
         text(356, 465, "BREWING", 12, "#edf4f7", 700, "middle"),
         '<rect x="324" y="491" width="64" height="75" fill="#2d424b"/><rect x="307" y="565" width="98" height="12" fill="#172a33"/>',
         '<path d="M280 600 L432 600 L420 640 Q356 660 292 640 Z" fill="#f7f8f3" stroke="#c8d2d2" stroke-width="2"/><path d="M300 625 L412 625 L410 640 Q356 654 302 640 Z" fill="#774b32"/>',
         '<path d="M356 578 L356 600" stroke="#a86938" stroke-width="5" stroke-dasharray="5 4"/>',
         text(355, 694, "VIRTUAL MUG · 54% FILLED", 13, "#edf4f7", 700, "middle"),
         text(96, 743, "Heater / pump are virtual · mug fill is illustrative", 13, "#f0c15a", 600),
         '<rect x="652" y="340" width="472" height="198" rx="14" fill="#192633"/>',
         text(674, 373, "LIVE READINGS", 13, "#9aadb9", 700),
         '<rect x="674" y="391" width="202" height="58" rx="10" fill="#223342"/><rect x="892" y="391" width="210" height="58" rx="10" fill="#223342"/>',
         text(691, 413, "STATE", 11, "#9aadb9", 700), text(691, 438, "BREWING", 18, "#54d6c5", 700),
         text(909, 413, "WATER TEMP", 11, "#9aadb9", 700), text(909, 438, "92.4 °C", 18, "#edf4f7", 700),
         '<rect x="674" y="463" width="202" height="58" rx="10" fill="#223342"/><rect x="892" y="463" width="210" height="58" rx="10" fill="#223342"/>',
         text(691, 485, "CUP / DISTANCE", 11, "#9aadb9", 700), text(691, 510, "PRESENT · 63 mm", 16, "#62d58b", 700),
         text(909, 485, "STRENGTH / FAULT", 11, "#9aadb9", 700), text(909, 510, "NORMAL · NONE", 16, "#edf4f7", 700),
         '<rect x="652" y="552" width="472" height="123" rx="14" fill="#192633"/>',
         text(674, 582, "BOARD INDICATORS", 13, "#9aadb9", 700),
         '<circle cx="686" cy="614" r="8" fill="#455761"/><circle cx="802" cy="614" r="8" fill="#f0c15a"/><circle cx="920" cy="614" r="8" fill="#455761"/><circle cx="1040" cy="614" r="8" fill="#54d6c5"/>',
         text(701, 620, "READY", 12, "#edf4f7", 500), text(817, 620, "HEATING", 12, "#edf4f7", 500), text(935, 620, "FAULT", 12, "#edf4f7", 500), text(1055, 620, "HEARTBEAT", 12, "#edf4f7", 500),
         '<rect x="652" y="689" width="472" height="83" rx="14" fill="#192633"/>',
         text(674, 718, "SIMULATED ACTUATORS", 12, "#9aadb9", 700),
         text(674, 749, "HEATER  0%", 16, "#f0c15a", 700),
         text(900, 749, "PUMP  RUNNING · 10.8 / 20 s", 15, "#54d6c5", 700),
         text(674, 765, "QSPI EVENT LOG · ONLINE", 11, "#62d58b", 700),
         '<rect x="1158" y="202" width="384" height="590" rx="20" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
         text(1190, 248, "SCREENSHOT GUIDE", 17, "#8fa8b3", 700)]
    callouts = [
        ("1", "Connection", "Choose ST-LINK COM port; live / stale / offline is shown at the top."),
        ("2", "Sensors and state", "Temperature, ToF cup distance, strength, fault, and FSM state mirror telemetry."),
        ("3", "Board LEDs", "Four lamps mirror PB4, PB1, PA4, and PB14 output levels."),
        ("4", "Pump + QSPI history", "Pump timer drives virtual mug fill. Read QSPI log to view history and save CSV."),
    ]
    for i, (num, title, desc) in enumerate(callouts):
        y = 300 + i*116
        lines = wrap(desc, 35)
        a += [f'<circle cx="1207" cy="{y-4}" r="17" fill="#20534e"/>',
              text(1207, y+2, num, 16, "#bff8ef", 700, "middle"),
              text(1240, y+2, title, 19, "#edf4f7", 700)]
        for j, line in enumerate(lines):
            a.append(text(1240, y+31+j*23, line, 15, "#c2d0d7", 400))
    a += [text(1190, 759, "Read-only control · QSPI read request only", 14, "#f0c15a", 650),
          text(58, 832, "Illustrative dashboard screenshot layout · connect USART1 at 115200 8N1 to show live controller telemetry", 18, "#e3e9eb", 500),
          text(1542, 832, "10 / 14", 17, "#9db1bb", 700, "end"),
          text(58, 886, "The live machine graphic, actuator values, timer, and LEDs update from STATUS packets; no actuator is physically controlled.", 14, "#80959f", 500),
          '</svg>']
    return "\n".join(a)


(OUT / "COFFEE_MAKER_STEP_09_QSPI_STORAGE.svg").write_text(storage_slide(), encoding="utf-8")
(OUT / "COFFEE_MAKER_STEP_10_GUI_TOUR.svg").write_text(gui_slide(), encoding="utf-8")


def card(x, y, w, h, heading, lines, accent="#62d7c6", fill="#172733"):
    out = [f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="20" fill="{fill}" stroke="#35505e" stroke-width="2"/>',
           text(x+24, y+39, heading, 18, accent, 700)]
    for row, line in enumerate(lines):
        for part, wrapped in enumerate(wrap(line, max(24, int((w-48)/11)))):
            out.append(text(x+24, y+78+row*64+part*25, wrapped, 19, "#e3ecef", 500))
    return "\n".join(out)


def code_slide(num, title, subtitle, body):
    return "\n".join([
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        '<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#14212c"/><stop offset="1" stop-color="#0b131b"/></linearGradient><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="9" markerHeight="9" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="#58d6c6"/></marker><marker id="redarr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="9" markerHeight="9" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="#ff6a70"/></marker></defs>',
        '<rect width="1600" height="900" fill="url(#bg)"/>',
        text(58, 70, "COFFEE MAKER · FIRMWARE IMPLEMENTATION", 18, "#62d7c6", 700),
        text(58, 126, title, 43, "#f3f7f8", 700),
        text(60, 164, subtitle, 22, "#aec0c8"),
        body,
        text(1542, 832, f"{num:02d} / {TOTAL_STEPS:02d}", 17, "#9db1bb", 700, "end"),
        f'<rect x="58" y="856" width="{1484*num/TOTAL_STEPS:.1f}" height="4" rx="2" fill="#58d6c6"/>',
        text(58, 886, "Coffee application is integrated in source · coffee-mode hardware behavior still needs validation", 14, "#80959f", 500),
        '</svg>'
    ])


def main_flow_slide():
    b = ['<rect x="58" y="218" width="1484" height="548" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>']
    steps = [
        ("1 · STARTUP", "main.c", ["HAL and CubeMX peripheral setup", "IWDG starts"], "#62d7c6"),
        ("2 · MODE SELECT", "app_config.h", ["0 → CoffeeApp", "1 → IoDiagnostics"], "#62d7c6"),
        ("3 · TASK LOOP", "main.c", ["Init once, then call selected task", "Each pass refreshes IWDG"], "#62d7c6"),
        ("4 · REPEAT", "while (1)", ["Run task again", "Keep watchdog serviced"], "#62d7c6"),
    ]
    xs = [82, 455, 828, 1201]
    for i, (head, file, lines, accent) in enumerate(steps):
        b.append(card(xs[i], 302, 320, 250, head, [file, *lines], accent, "#203744"))
        if i < 3:
            b.append(f'<path d="M{xs[i]+323} 426 L{xs[i]+361} 426" stroke="#58d6c6" stroke-width="5" marker-end="url(#arr)"/>')
    b += ['<path d="M1360 570 C1360 662 972 662 972 570" fill="none" stroke="#58d6c6" stroke-width="4" marker-end="url(#arr)"/>',
          '<rect x="190" y="632" width="1220" height="82" rx="16" fill="#14222c"/>',
          text(220, 667, "CURRENT SETTING", 16, "#62d7c6", 700),
          text(220, 698, "IO_DIAGNOSTICS_MODE = 0 → coffee application starts; set to 1 to return to the confirmed GPIO diagnostic mode.", 19, "#e5edf0", 500),
          text(82, 744, "Application hooks live in CubeMX USER CODE sections so code generation preserves the selection and loop.", 16, "#a8bac3", 500)]
    return code_slide(11, "11. Start at main.c: the firmware call path", "The main loop owns initialization, task selection, repeat scheduling, and watchdog refresh.", "\n".join(b))


def app_task_slide():
    b = ['<rect x="58" y="218" width="1484" height="548" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>']
    blocks = [
        ("1 · READ INPUTS", "coffee_app.c", ["PC13 / PD14 buttons: 30 ms qualification", "PA3 strength switch", "PC5 ADC → 20–110 °C simulated temp", "VL53L0X: 100 ms reads; 80 / 100 mm hysteresis"], "#62d7c6"),
        ("2 · BUILD INPUT", "CoffeeSmInput_t", ["Current readings and valid flags", "Button events + selected strength", "Elapsed time since prior task pass"], "#62d7c6"),
        ("3 · DECIDE", "coffee_sm_step()", ["HAL-free state-machine update", "Returns state, fault, LED patterns", "Returns virtual heater / pump requests"], "#f0c15a"),
        ("4 · APPLY + REPORT", "coffee_app.c", ["Drive GPIO LED patterns", "Append events and stream QSPI records", "Send USART1 STATUS every 100 ms", "Accept CMD,READ_LOGS from GUI"], "#62d7c6"),
    ]
    xs = [78, 451, 824, 1197]
    for i, (head, file, lines, accent) in enumerate(blocks):
        b.append(card(xs[i], 282, 324, 386, head, [file, *lines], accent, "#203744"))
        if i < 3:
            b.append(f'<path d="M{xs[i]+326} 467 L{xs[i]+359} 467" stroke="#58d6c6" stroke-width="5" marker-end="url(#arr)"/>')
    b += [text(92, 748, "ToF init is retried after startup / range errors; invalid distance is cleared and reported as cup_valid=0.", 18, "#c4d1d7", 500)]
    return code_slide(12, "12. CoffeeApp_Task: collect → decide → act", "CoffeeApp_Task is the application coordinator between hardware readings, the FSM, and its output/reporting blocks.", "\n".join(b))


def fsm_slide():
    b = ['<rect x="58" y="218" width="1484" height="548" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
         text(90, 263, "HAL-FREE DECISION ENGINE · coffee_sm.c / coffee_sm.h", 17, "#8fa8b3", 700)]
    names = [("STANDBY", "Wait for power"), ("IDLE", "Cup + start"), ("HEATING", "Below target"), ("BREWING", "Timed phases"), ("COMPLETE", "Stop / return")]
    xs = [85, 380, 675, 970, 1265]
    for i, (name, hint) in enumerate(names):
        b += [f'<rect x="{xs[i]}" y="305" width="230" height="112" rx="18" fill="#203744" stroke="#3d6870" stroke-width="2"/>',
              text(xs[i]+115, 348, name, 20, "#edf4f7", 700, "middle"),
              text(xs[i]+115, 384, hint, 16, "#b9cbd2", 500, "middle")]
        if i < len(names)-1:
            b.append(f'<path d="M{xs[i]+233} 361 L{xs[i]+286} 361" stroke="#58d6c6" stroke-width="4" marker-end="url(#arr)"/>')
    b += ['<path d="M493 417 C493 520 202 520 202 417" fill="none" stroke="#58d6c6" stroke-width="3" marker-end="url(#arr)"/>',
          '<path d="M495 417 C495 444 1085 444 1085 417" fill="none" stroke="#f0c15a" stroke-width="3" marker-end="url(#arr)"/>',
          text(790, 461, "Already at target", 13, "#f1c66f", 600, "middle"),
          '<path d="M1380 304 L1380 282 L495 282 L495 300" fill="none" stroke="#58d6c6" stroke-width="3" marker-end="url(#arr)"/>',
          '<path d="M495 417 L495 552 M790 417 L790 552 M1085 417 L1085 552 M1380 417 L1380 552 M495 552 L1380 552 M790 552 L790 578" fill="none" stroke="#ff6a70" stroke-width="3" stroke-dasharray="8 7" marker-end="url(#redarr)"/>',
          text(90, 548, "FAULT CHECKS", 14, "#ff888d", 700),
          '<rect x="570" y="470" width="460" height="72" rx="16" fill="#392d20" stroke="#8a6737" stroke-width="2"/>',
          text(800, 500, "BREW PHASES · pump active-time timer", 16, "#f1c66f", 700, "middle"),
          text(800, 526, "Pre-infusion 2 / 5 s → bloom pause 2 s OFF → extract to 20 / 32 s ON", 15, "#f0e9dc", 500, "middle"),
          '<rect x="650" y="588" width="300" height="88" rx="18" fill="#3a2428" stroke="#87474d" stroke-width="2"/>',
          text(800, 623, "FAULT", 20, "#ff9c9e", 700, "middle"),
          text(800, 652, "safe outputs · red 4 Hz · recovery check", 15, "#f0d9da", 500, "middle"),
          text(90, 594, "SAFETY RULES", 16, "#ff888d", 700),
          text(90, 630, "No cup at start → ignore. Cup lost ≥200 ms during heating/brewing → fault.", 17, "#e5edf0", 500),
          text(90, 661, "Over 102 °C · 180 s heat timeout · sensor communication failure → fault.", 17, "#e5edf0", 500),
          text(90, 719, "The FSM returns commands and patterns; `coffee_app.c` applies them to pins, flash logging, and telemetry.", 17, "#b7c8cf", 500)]
    return code_slide(13, "13. The FSM is the decision core", "It owns state transitions and safety decisions; hardware code stays outside the state-machine module.", "\n".join(b))


def interfaces_slide():
    b = ['<rect x="58" y="218" width="1484" height="548" rx="26" fill="#172733" stroke="#2b414f" stroke-width="2"/>',
         card(90, 290, 410, 352, "INPUT BLOCKS", ["GPIO · PC13 power, PD14 start, PA3 strength", "ADC1 · PC5 simulated temperature", "I2C2 · VL53L0X distance / cup validity", "Source: coffee_app.c + vendor sensor API"], "#62d7c6", "#203744"),
         card(596, 345, 410, 242, "APPLICATION CORE", ["coffee_app.c builds inputs", "coffee_sm.c decides outputs", "io_diagnostics.c can verify I/O"], "#f0c15a", "#273742"),
         card(1100, 290, 410, 352, "OUTPUT / DATA BLOCKS", ["GPIO · PB4 ready, PB1 heat, PA4 fault, PB14 heartbeat", "QSPI · sectors 1–2 ToF profile; 3–15 event log", "USART1 · STATUS + READ_LOGS ↔ Python GUI", "Source: calibration store + storage manager"], "#62d7c6", "#203744"),
         '<path d="M505 466 L585 466" stroke="#58d6c6" stroke-width="5" marker-end="url(#arr)"/>',
         '<path d="M1010 466 L1089 466" stroke="#58d6c6" stroke-width="5" marker-end="url(#arr)"/>',
         '<rect x="90" y="672" width="1420" height="58" rx="14" fill="#14222c"/>',
         text(116, 708, "SEPARATION OF CONCERNS", 15, "#62d7c6", 700),
         text(408, 708, "main.c schedules · app coordinates · FSM decides · drivers touch hardware · GUI visualizes telemetry", 18, "#e3ecef", 500),
         text(90, 750, "USART1 accepts the QSPI read request only; no state/actuator commands. Heater and pump are simulated.", 16, "#f0c15a", 500)]
    return code_slide(14, "14. Expand outward: drivers and interfaces", "The application connects real sensor/control inputs to safe GPIO outputs, persistent event records, and the PC digital twin.", "\n".join(b))


for index, builder in enumerate((main_flow_slide, app_task_slide, fsm_slide, interfaces_slide), start=1):
    svg = builder()
    (OUT / f"COFFEE_MAKER_CODE_{index:02d}.svg").write_text(svg, encoding="utf-8")
