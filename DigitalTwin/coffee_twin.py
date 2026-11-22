"""Read-only serial dashboard and animated digital twin for the coffee maker."""

from __future__ import annotations

import csv
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    from serial import Serial, SerialException
    from serial.tools import list_ports
except ImportError:  # Dashboard remains usable as a UI preview without pyserial.
    Serial = None
    SerialException = Exception
    list_ports = None


BG = "#101923"
PANEL = "#192633"
PANEL_ALT = "#223342"
TEXT = "#edf4f7"
MUTED = "#9aadb9"
TEAL = "#54d6c5"
GREEN = "#62d58b"
YELLOW = "#f0c15a"
RED = "#ff6e70"


class CoffeeTwin:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("IoT Coffee Maker | Digital Twin")
        self.root.geometry("1180x760")
        self.root.minsize(980, 660)
        self.root.configure(bg=BG)

        self.rx_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.connection_id = 0
        self.reader_stop: threading.Event | None = None
        self.reader: threading.Thread | None = None
        self.serial_port = None
        self.last_packet_at: float | None = None
        self.connected = False
        self.data: dict[str, str] = {}
        self.animation_phase = 0
        self.last_state: str | None = None
        self.last_pump = False
        self.pump_elapsed_seconds = 0.0
        self.pump_active_since: float | None = None
        self.pending_log = False
        self.log_deadline: float | None = None
        self.log_scan_ack = False
        self.log_expected_count = 0
        self.log_records: list[dict[str, int]] = []
        self.log_window: tk.Toplevel | None = None
        self.log_tree: ttk.Treeview | None = None
        self.log_status_var: tk.StringVar | None = None
        self._build_ui()
        self.refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(50, self._process_queue)
        self.root.after(80, self._animate)

    def _build_ui(self) -> None:
        header = tk.Frame(self.root, bg=BG)
        header.pack(fill="x", padx=24, pady=(18, 12))
        tk.Label(header, text="COFFEE MAKER", bg=BG, fg=TEXT,
                 font=("Segoe UI", 21, "bold")).pack(side="left")
        tk.Label(header, text="DIGITAL TWIN  /  LIVE CONTROLLER VIEW", bg=BG,
                 fg=MUTED, font=("Segoe UI", 9, "bold")).pack(side="left", padx=16, pady=(8, 0))

        self.link_var = tk.StringVar(value="OFFLINE · Connect to the board")
        self.link_label = tk.Label(header, textvariable=self.link_var, bg=BG,
                                   fg=YELLOW, font=("Segoe UI", 10, "bold"))
        self.link_label.pack(side="right", pady=(8, 0))

        toolbar = tk.Frame(self.root, bg=PANEL, padx=12, pady=10)
        toolbar.pack(fill="x", padx=24, pady=(0, 16))
        tk.Label(toolbar, text="Serial port", bg=PANEL, fg=MUTED,
                 font=("Segoe UI", 9)).pack(side="left", padx=(2, 8))
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(toolbar, textvariable=self.port_var,
                                     width=22, state="normal")
        self.port_box.pack(side="left")
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(toolbar, textvariable=self.baud_var, values=("115200",),
                     width=9, state="readonly").pack(side="left", padx=8)
        self.connect_button = tk.Button(toolbar, text="Connect", command=self.connect,
                                        bg=TEAL, fg="#0b2024", relief="flat",
                                        padx=16, pady=6, font=("Segoe UI", 9, "bold"))
        self.connect_button.pack(side="left", padx=(4, 8))
        tk.Button(toolbar, text="↻ Ports", command=self.refresh_ports,
                  bg=PANEL_ALT, fg=TEXT, relief="flat", padx=12, pady=6,
                  font=("Segoe UI", 9)).pack(side="left")
        self.log_button = tk.Button(toolbar, text="Read QSPI log", command=self.request_qspi_log,
                                    bg=PANEL_ALT, fg=MUTED, relief="flat", padx=12, pady=6,
                                    font=("Segoe UI", 9, "bold"), state="disabled")
        self.log_button.pack(side="left", padx=(8, 0))
        tk.Label(toolbar, text="Telemetry + read-only QSPI · 115200 8N1",
                 bg=PANEL, fg=MUTED, font=("Segoe UI", 9)).pack(side="right", padx=4)

        content = tk.Frame(self.root, bg=BG)
        content.pack(fill="both", expand=True, padx=24, pady=(0, 18))
        content.columnconfigure(0, weight=3)
        content.columnconfigure(1, weight=2)
        content.rowconfigure(0, weight=1)

        self.machine_canvas = tk.Canvas(content, bg=PANEL, highlightthickness=0)
        self.machine_canvas.grid(row=0, column=0, sticky="nsew", padx=(0, 14))
        self.machine_canvas.bind("<Configure>", lambda _event: self._draw_machine())

        side = tk.Frame(content, bg=BG)
        side.grid(row=0, column=1, sticky="nsew")
        side.columnconfigure(0, weight=1)
        side.columnconfigure(1, weight=1)
        self.state_value = self._metric(side, 0, 0, "CONTROLLER STATE", "WAITING", TEAL)
        self.temp_value = self._metric(side, 0, 1, "WATER TEMP", "--.- °C", TEXT)
        self.cup_value = self._metric(side, 1, 0, "CUP SENSOR", "NO DATA", MUTED)
        self.distance_value = self._metric(side, 1, 1, "DISTANCE", "--- mm", TEXT)
        self.strength_value = self._metric(side, 2, 0, "BREW STRENGTH", "--", TEXT)
        self.fault_value = self._metric(side, 2, 1, "FAULT", "NONE", GREEN)

        led_panel = tk.Frame(side, bg=PANEL, padx=14, pady=12)
        led_panel.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        tk.Label(led_panel, text="BOARD INDICATORS", bg=PANEL, fg=MUTED,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 10))
        leds = tk.Frame(led_panel, bg=PANEL)
        leds.pack(fill="x")
        for name, color in (("READY · PB4", GREEN), ("HEATING · PB1", YELLOW),
                            ("FAULT · PA4", RED), ("HEARTBEAT · PB14", TEAL)):
            row = tk.Frame(leds, bg=PANEL)
            row.pack(fill="x", pady=4)
            dot = tk.Canvas(row, width=18, height=18, bg=PANEL, highlightthickness=0)
            dot.pack(side="left", padx=(0, 9))
            dot.create_oval(3, 3, 15, 15, fill="#42505c", outline="")
            tk.Label(row, text=name, bg=PANEL, fg=TEXT,
                     font=("Segoe UI", 9)).pack(side="left")
            setattr(self, f"led_canvas_{name.split(' ·')[0].lower()}", dot)

        self.actuator_panel = tk.Frame(side, bg=PANEL, padx=14, pady=12)
        self.actuator_panel.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        tk.Label(self.actuator_panel, text="SIMULATED ACTUATORS", bg=PANEL, fg=MUTED,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 8))
        self.heater_var = tk.StringVar(value="HEATER  ·  0%")
        self.pump_var = tk.StringVar(value="PUMP  ·  OFF")
        tk.Label(self.actuator_panel, textvariable=self.heater_var, bg=PANEL, fg=YELLOW,
                 font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=3)
        tk.Label(self.actuator_panel, textvariable=self.pump_var, bg=PANEL, fg=TEAL,
                 font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=3)

        self.note_var = tk.StringVar(value="Connect to USART1 to show live controller outputs.")
        tk.Label(self.root, textvariable=self.note_var, bg=BG, fg=MUTED,
                 font=("Segoe UI", 9)).pack(anchor="w", padx=26, pady=(0, 10))

    def _metric(self, parent: tk.Widget, row: int, col: int, title: str,
                value: str, color: str) -> tk.Label:
        card = tk.Frame(parent, bg=PANEL, padx=13, pady=11)
        card.grid(row=row, column=col, sticky="nsew", padx=(0, 8 if col == 0 else 0),
                  pady=(0, 8))
        tk.Label(card, text=title, bg=PANEL, fg=MUTED,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w")
        value_label = tk.Label(card, text=value, bg=PANEL, fg=color,
                               font=("Segoe UI", 15, "bold"))
        value_label.pack(anchor="w", pady=(7, 0))
        return value_label

    def refresh_ports(self) -> None:
        if list_ports is None:
            self.port_box["values"] = ()
            if not self.port_var.get():
                self.port_var.set("COMx")
            self.note_var.set("Install pyserial to list ports and connect: pip install pyserial")
            return
        ports = [port.device for port in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])
        elif not ports and not self.port_var.get():
            self.port_var.set("COMx")

    def connect(self) -> None:
        if self.connected:
            self._disconnect()
            return
        if Serial is None:
            messagebox.showerror("pyserial required", "Install pyserial with: pip install pyserial")
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showinfo("Select a port", "Choose the ST-LINK virtual COM port first.")
            return
        try:
            self.serial_port = Serial(port, int(self.baud_var.get()), timeout=0.2)
        except (SerialException, ValueError) as exc:
            messagebox.showerror("Connection failed", str(exc))
            return
        self.connected = True
        self.pending_log = False
        self.log_deadline = None
        self.log_scan_ack = False
        self.log_button.configure(state="disabled")
        self.last_packet_at = None
        self.data = {}
        self.connection_id += 1
        current_connection = self.connection_id
        self.reader_stop = threading.Event()
        self.last_state = None
        self.last_pump = False
        self.pump_elapsed_seconds = 0.0
        self.pump_active_since = None
        self.reader = threading.Thread(target=self._reader_loop,
                                       args=(current_connection, self.reader_stop,
                                             self.serial_port), daemon=True)
        self.reader.start()
        self.connect_button.configure(text="Disconnect", bg=RED, fg="#2a1115")
        self.link_var.set(f"CONNECTED · {port} · waiting for STATUS")
        self.link_label.configure(fg=YELLOW)
        self.note_var.set("Waiting for STATUS telemetry. Diagnostic DIAG lines are intentionally ignored.")

    def _reader_loop(self, connection_id: int, stop_event: threading.Event,
                     port: object) -> None:
        while not stop_event.is_set():
            try:
                raw = port.readline()
                if raw:
                    self.rx_queue.put(("line", (connection_id,
                                                 raw.decode("ascii", errors="replace").strip())))
            except (SerialException, OSError) as exc:
                if not stop_event.is_set():
                    self.rx_queue.put(("error", (connection_id, str(exc))))
                break
        self.rx_queue.put(("ended", connection_id))

    def _disconnect(self) -> None:
        was_downloading_log = self.pending_log
        self.connection_id += 1
        if self.reader_stop is not None:
            self.reader_stop.set()
        if self.serial_port is not None:
            try:
                self.serial_port.close()
            except Exception:
                pass
        self.serial_port = None
        self.reader_stop = None
        self.connected = False
        self.pending_log = False
        self.log_button.configure(state="disabled")
        if was_downloading_log and self.log_status_var is not None:
            self.log_status_var.set("Disconnected before the QSPI transfer finished.")
        self.last_packet_at = None
        self.data = {}
        self.last_state = None
        self.last_pump = False
        self.pump_elapsed_seconds = 0.0
        self.pump_active_since = None
        self.connect_button.configure(text="Connect", bg=TEAL, fg="#0b2024")
        self.link_var.set("OFFLINE · Connect to the board")
        self.link_label.configure(fg=YELLOW)
        self._set_actuators_stopped()
        self.pump_var.set("PUMP  ·  OFF / LINK LOST  ·  0.0 s")
        self.state_value.configure(text="OFFLINE", fg=MUTED)
        self.temp_value.configure(text="--.- °C", fg=MUTED)
        self.cup_value.configure(text="NO DATA", fg=MUTED)
        self.distance_value.configure(text="--- mm", fg=MUTED)
        self.strength_value.configure(text="--", fg=MUTED)
        self.fault_value.configure(text="--", fg=MUTED)
        for led in ("ready", "heating", "fault", "heartbeat"):
            self._set_led(led, False)
        self._draw_machine()

    def _process_queue(self) -> None:
        while True:
            try:
                kind, payload = self.rx_queue.get_nowait()
            except queue.Empty:
                break
            if kind in ("line", "error"):
                event_connection_id, event_payload = payload
                if event_connection_id != self.connection_id:
                    continue
                if kind == "line":
                    self._parse_line(str(event_payload))
                else:
                    self.note_var.set(f"Serial read error: {event_payload}")
            elif kind == "ended" and payload == self.connection_id and self.connected:
                self._disconnect()
        if (self.pending_log and self.log_deadline is not None and
                time.monotonic() >= self.log_deadline):
            if self.log_scan_ack:
                message = ("STM32 started the QSPI scan but did not finish it. "
                           "Check the QSPI connection/flash and reset the board.")
            elif (self.last_packet_at is not None and
                  time.monotonic() - self.last_packet_at <= 0.5):
                message = ("No acknowledgment for CMD,READ_LOGS, but USART1 telemetry is live. "
                           "The board is running; reconnect the COM port and verify the latest "
                           "coffee-app firmware was built and flashed.")
            else:
                message = ("No USART1 response from the STM32. Check that the COM port is "
                           "connected and the coffee-app firmware is running; calibration "
                           "and GPIO diagnostic modes do not serve this command.")
            self._finish_log_request(message)
        self.root.after(50, self._process_queue)

    def _parse_line(self, line: str) -> None:
        if line == "LOG_SCAN":
            if not self.pending_log:
                return
            self.log_scan_ack = True
            self.log_deadline = time.monotonic() + 20.0
            if self.log_status_var is not None:
                self.log_status_var.set("STM32 is scanning QSPI records…")
            return
        if line.startswith("LOG_BEGIN,"):
            if not self.pending_log:
                return
            fields = self._parse_fields(line.partition(",")[2])
            try:
                count = int(fields["count"])
            except (KeyError, ValueError):
                self._finish_log_request("Flash returned a malformed log header.")
                return
            if count < 0 or count > 1664:
                self._finish_log_request("Flash reported an invalid record count.")
                return
            self.log_expected_count = count
            self.log_deadline = time.monotonic() + 5.0
            self.log_records = []
            self._ensure_log_window()
            if self.log_status_var is not None:
                self.log_status_var.set(f"Receiving 0 / {count} records…")
            return
        if line.startswith("LOG,"):
            if not self.pending_log:
                return
            fields = self._parse_fields(line.partition(",")[2])
            required = {"seq", "tick_ms", "event", "state", "fault", "temp_x10",
                        "cup_mm", "strength", "flags", "heater", "pump", "crc"}
            if not required.issubset(fields):
                self._finish_log_request("Flash returned an incomplete record.")
                return
            try:
                record = {key: int(fields[key], 16) if key == "crc" else int(fields[key])
                          for key in required}
            except ValueError:
                self._finish_log_request("Flash returned a non-numeric record field.")
                return
            self.log_records.append(record)
            self.log_deadline = time.monotonic() + 3.0
            if self.log_status_var is not None:
                self.log_status_var.set(
                    f"Receiving {len(self.log_records)} / {self.log_expected_count} records…")
            return
        if line.startswith("LOG_END,"):
            if not self.pending_log:
                return
            fields = self._parse_fields(line.partition(",")[2])
            try:
                end_count = int(fields["count"])
            except (KeyError, ValueError):
                self._finish_log_request("Flash returned a malformed end marker.")
                return
            self.log_records.sort(key=lambda record: record["seq"])
            self._populate_log_table()
            self._finish_log_request(
                f"Received {len(self.log_records)} of {self.log_expected_count} records."
                if end_count == len(self.log_records) == self.log_expected_count
                else f"Incomplete download: received {len(self.log_records)} of {self.log_expected_count} records.")
            return
        if line.startswith("ERR,"):
            if not self.pending_log:
                return
            fields = self._parse_fields(line.partition(",")[2])
            error = fields.get("code", "UNKNOWN")
            self._finish_log_request(f"QSPI read failed: {error}")
            return
        if not line.startswith("STATUS,"):
            return
        fields = self._parse_fields(line.partition(",")[2])
        if not self._valid_status(fields):
            self.note_var.set("Ignored malformed STATUS line; waiting for a complete controller packet.")
            return
        now = time.monotonic()
        state = fields["state"]
        pump = fields["pump"] == "1"
        if state == "BREWING" and self.last_state != "BREWING":
            self.pump_elapsed_seconds = 0.0
            self.pump_active_since = now if pump else None
        elif pump and not self.last_pump:
            self.pump_active_since = now
        elif self.last_pump and not pump and self.pump_active_since is not None:
            self.pump_elapsed_seconds += now - self.pump_active_since
            self.pump_active_since = None
        if self.last_state == "BREWING" and state != "BREWING" and self.pump_active_since is not None:
            self.pump_elapsed_seconds += now - self.pump_active_since
            self.pump_active_since = None
        self.last_state = state
        self.last_pump = pump
        self.data = fields
        self.last_packet_at = now
        self._render_status()

    @staticmethod
    def _parse_fields(payload: str) -> dict[str, str]:
        fields: dict[str, str] = {}
        for part in payload.split(","):
            key, sep, value = part.partition("=")
            if sep:
                fields[key.strip()] = value.strip()
        return fields

    def request_qspi_log(self) -> None:
        if not self.connected or self.serial_port is None:
            return
        if self.last_packet_at is None or time.monotonic() - self.last_packet_at > 0.5:
            self.note_var.set("Wait for fresh controller telemetry before reading the QSPI log.")
            return
        if self.data.get("flash") != "OK":
            self.note_var.set("The STM32 reports that the QSPI flash interface is unavailable.")
            return
        self._ensure_log_window()
        self.log_records = []
        self.log_expected_count = 0
        self.log_scan_ack = False
        self.log_deadline = time.monotonic() + 5.0
        if self.log_tree is not None:
            self.log_tree.delete(*self.log_tree.get_children())
        self.pending_log = True
        self.log_button.configure(state="disabled")
        self.link_var.set("READING · QSPI EVENT LOG")
        self.link_label.configure(fg=TEAL)
        if self.log_status_var is not None:
            self.log_status_var.set("Requesting records from the STM32…")
        try:
            self.serial_port.write(b"CMD,READ_LOGS\r\n")
            self.serial_port.flush()
        except (SerialException, OSError) as exc:
            self._finish_log_request(f"Could not send QSPI read request: {exc}")

    def _ensure_log_window(self) -> None:
        if self.log_window is not None and self.log_window.winfo_exists():
            self.log_window.deiconify()
            self.log_window.lift()
            return
        window = tk.Toplevel(self.root)
        window.title("Coffee Maker | QSPI Event Log")
        window.geometry("1220x590")
        window.minsize(900, 420)
        window.configure(bg=BG)
        self.log_window = window
        self.log_status_var = tk.StringVar(value="No records downloaded yet.")
        header = tk.Frame(window, bg=BG)
        header.pack(fill="x", padx=18, pady=(16, 10))
        tk.Label(header, text="QSPI EVENT LOG", bg=BG, fg=TEXT,
                 font=("Segoe UI", 15, "bold")).pack(side="left")
        tk.Label(header, textvariable=self.log_status_var, bg=BG, fg=MUTED,
                 font=("Segoe UI", 9)).pack(side="right")
        table_frame = tk.Frame(window, bg=BG)
        table_frame.pack(fill="both", expand=True, padx=18, pady=(0, 10))
        columns = ("seq", "tick_ms", "event_id", "event", "state", "fault",
                   "temp_c", "cup_mm", "flags", "strength", "heater", "pump", "crc")
        tree = ttk.Treeview(table_frame, columns=columns, show="headings")
        headings = {
            "seq": "Sequence", "tick_ms": "Uptime (ms)", "event_id": "Event ID",
            "event": "Event", "state": "State", "fault": "Fault", "temp_c": "Temp (°C)",
            "cup_mm": "Cup (mm)", "flags": "Valid inputs", "strength": "Strength",
            "heater": "Heater %", "pump": "Pump", "crc": "CRC16",
        }
        widths = {"seq": 72, "tick_ms": 100, "event_id": 75, "event": 155,
                  "state": 90, "fault": 110, "temp_c": 85, "cup_mm": 85,
                  "flags": 160, "strength": 80, "heater": 75, "pump": 65, "crc": 75}
        for column in columns:
            tree.heading(column, text=headings[column])
            tree.column(column, width=widths[column], minwidth=60, stretch=True,
                        anchor="center")
        scrollbar = ttk.Scrollbar(table_frame, orient="vertical", command=tree.yview)
        xscroll = ttk.Scrollbar(table_frame, orient="horizontal", command=tree.xview)
        tree.configure(yscrollcommand=scrollbar.set, xscrollcommand=xscroll.set)
        tree.grid(row=0, column=0, sticky="nsew")
        scrollbar.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        table_frame.rowconfigure(0, weight=1)
        table_frame.columnconfigure(0, weight=1)
        self.log_tree = tree
        actions = tk.Frame(window, bg=BG)
        actions.pack(fill="x", padx=18, pady=(0, 14))
        tk.Button(actions, text="Save CSV…", command=self.save_log_csv,
                  bg=TEAL, fg="#0b2024", relief="flat", padx=14, pady=7,
                  font=("Segoe UI", 9, "bold")).pack(side="right")
        window.protocol("WM_DELETE_WINDOW", self._close_log_window)

    def _close_log_window(self) -> None:
        if self.log_window is not None:
            self.log_window.destroy()
        self.log_window = None
        self.log_tree = None
        self.log_status_var = None

    def _populate_log_table(self) -> None:
        if self.log_tree is None or not self.log_tree.winfo_exists():
            return
        self.log_tree.delete(*self.log_tree.get_children())
        for record in self.log_records:
            self.log_tree.insert("", "end", values=self._format_log_row(record))

    @staticmethod
    def _format_log_row(record: dict[str, int]) -> tuple[str, ...]:
        states = ("STANDBY", "IDLE", "HEATING", "BREWING", "COMPLETE", "FAULT")
        faults = ("NONE", "NO_CUP", "OVERTEMP", "HEAT_TIMEOUT", "SENSOR_COMM", "TANK_EMPTY")
        strength = ("NORMAL", "STRONG")
        state = states[record["state"]] if record["state"] < len(states) else "UNKNOWN"
        fault = faults[record["fault"]] if record["fault"] < len(faults) else "UNKNOWN"
        brew_strength = strength[record["strength"]] if record["strength"] < len(strength) else "UNKNOWN"
        event_id = record["event"]
        if event_id == 0x0100:
            event = "BOOT"
        else:
            event_names = ((1, "STATE_CHANGED"), (2, "START"), (4, "START_IGNORED"),
                           (8, "CANCEL"), (16, "BREW_COMPLETE"), (32, "FAULT_SET"),
                           (64, "FAULT_CLEARED"))
            event = "|".join(name for bit, name in event_names if event_id & bit) or "NONE"
        flags = record["flags"]
        flag_names = ((1, "TEMP_OK"), (2, "TOF_OK"), (4, "CUP_PRESENT"))
        valid_flags = "|".join(name for bit, name in flag_names if flags & bit) or "NONE"
        return (str(record["seq"]), str(record["tick_ms"]), str(event_id), event,
                state, fault, f"{record['temp_x10'] / 10:.1f}",
                str(record["cup_mm"]) if flags & 2 else "--", valid_flags,
                brew_strength, str(record["heater"]), "ON" if record["pump"] else "OFF",
                f"{record['crc']:04X}")

    def save_log_csv(self) -> None:
        if not self.log_records:
            messagebox.showinfo("No records", "Download the QSPI event log before saving a CSV.")
            return
        path = filedialog.asksaveasfilename(
            parent=self.log_window or self.root,
            title="Save QSPI event log",
            defaultextension=".csv",
            filetypes=(("CSV files", "*.csv"), ("All files", "*.*")),
            initialfile="coffee_maker_qspi_log.csv")
        if not path:
            return
        fieldnames = ("seq", "tick_ms", "event", "state", "fault", "temp_x10",
                      "cup_mm", "strength", "flags", "heater", "pump", "crc")
        try:
            with open(path, "w", newline="", encoding="utf-8") as csv_file:
                writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(self.log_records)
        except OSError as exc:
            messagebox.showerror("Save failed", str(exc))
            return
        if self.log_status_var is not None:
            self.log_status_var.set(f"Saved {len(self.log_records)} records to {path}")

    def _finish_log_request(self, message: str) -> None:
        self.pending_log = False
        self.log_deadline = None
        telemetry_fresh = self.last_packet_at is not None and \
            time.monotonic() - self.last_packet_at <= 0.5
        self.log_button.configure(state="normal" if self.connected and telemetry_fresh and
                                  self.data.get("flash") == "OK" else "disabled")
        if self.log_status_var is not None:
            self.log_status_var.set(message)
        if self.connected:
            self.link_var.set("CONNECTED · QSPI log request ended")
            self.link_label.configure(fg=GREEN)

    @classmethod
    def _valid_status(cls, data: dict[str, str]) -> bool:
        required = {
            "state", "temp_x10", "target_x10", "cup_mm", "cup_valid", "cup",
            "strength", "heater", "pump", "ready_led", "heating_led", "fault_led",
            "heartbeat_led", "flash", "logger", "fault",
        }
        if not required.issubset(data):
            return False
        if data["state"] not in {"STANDBY", "IDLE", "HEATING", "BREWING", "COMPLETE", "FAULT"}:
            return False
        if data["fault"] not in {"NONE", "NO_CUP", "OVERTEMP", "HEAT_TIMEOUT", "SENSOR_COMM", "TANK_EMPTY"}:
            return False
        if (data["strength"] not in {"NORMAL", "STRONG"} or
                data["flash"] not in {"OK", "NO"} or
                data["logger"] not in {"OK", "NO"}):
            return False
        numeric_ranges = {
            "temp_x10": (200, 1100), "target_x10": (0, 1100), "cup_mm": (0, 65535),
            "heater": (0, 100),
        }
        for key, (minimum, maximum) in numeric_ranges.items():
            value = cls._number(data, key)
            if value is None or not minimum <= value <= maximum:
                return False
        for key in ("cup_valid", "cup", "pump", "ready_led", "heating_led", "fault_led", "heartbeat_led"):
            if data[key] not in {"0", "1"}:
                return False
        if data["cup_valid"] == "0" and (data["cup"] != "0" or data["cup_mm"] != "0"):
            return False
        return True

    def _render_status(self) -> None:
        d = self.data
        state = d.get("state", "UNKNOWN").upper()
        self.state_value.configure(text=state,
                                   fg=RED if state == "FAULT" else TEAL)
        temp = self._number(d, "temp_x10")
        target = self._number(d, "target_x10")
        self.temp_value.configure(text=f"{temp / 10:.1f} °C" if temp is not None else "--.- °C")
        self.cup_value.configure(text=("PRESENT" if d.get("cup") == "1" else "ABSENT")
                                 if d.get("cup_valid") == "1" else "NO DATA",
                                 fg=GREEN if d.get("cup_valid") == "1" and d.get("cup") == "1" else MUTED)
        distance = self._number(d, "cup_mm")
        self.distance_value.configure(text=f"{distance} mm"
                                       if d.get("cup_valid") == "1" and distance is not None
                                       else "--- mm")
        self.strength_value.configure(text=d.get("strength", "--"))
        fault = d.get("fault", "NONE").upper()
        self.fault_value.configure(text=fault, fg=RED if fault != "NONE" else GREEN)
        heater = self._number(d, "heater") or 0
        pump = d.get("pump", "0") == "1"
        self.heater_var.set(f"HEATER  ·  {heater}%" + (f"  /  TARGET {target / 10:.1f} °C" if target else ""))
        brew_target = 32 if d.get("strength") == "STRONG" else 20
        elapsed = self.pump_elapsed_seconds
        if pump and self.pump_active_since is not None:
            elapsed += time.monotonic() - self.pump_active_since
        self.pump_var.set(f"PUMP  ·  {'RUNNING' if pump else 'PAUSED' if d.get('state') == 'BREWING' else 'OFF'}"
                          f"  ·  {elapsed:.1f} / {brew_target} s")
        flash = "INITIALIZED" if d.get("flash") == "OK" else "NOT INITIALIZED"
        logger = "READY" if d.get("logger") == "OK" else "RETRYING"
        self._set_led("ready", d.get("ready_led") == "1")
        self._set_led("heating", d.get("heating_led") == "1")
        self._set_led("fault", d.get("fault_led") == "1")
        self._set_led("heartbeat", d.get("heartbeat_led") == "1")
        self.link_var.set("LIVE · USART1 telemetry")
        self.link_label.configure(fg=GREEN)
        self.note_var.set(f"QSPI interface {flash.lower()} · event logger {logger} · Live data · heater and pump are virtual outputs only.")
        self.log_button.configure(state="normal" if flash == "INITIALIZED" and not self.pending_log
                                  else "disabled")

    @staticmethod
    def _number(data: dict[str, str], key: str) -> int | None:
        try:
            return int(data[key])
        except (KeyError, ValueError):
            return None

    def _set_led(self, name: str, on: bool) -> None:
        canvas = getattr(self, f"led_canvas_{name}")
        color = GREEN if name == "ready" else YELLOW if name == "heating" else RED if name == "fault" else TEAL
        canvas.itemconfigure(1, fill=color if on else "#42505c")

    def _set_actuators_stopped(self) -> None:
        self.heater_var.set("HEATER  ·  0%  /  LINK LOST")
        self.pump_var.set("PUMP  ·  OFF  /  LINK LOST")

    def _animate(self) -> None:
        stale = self.connected and not self.pending_log and (self.last_packet_at is None or
                                    time.monotonic() - self.last_packet_at > 0.5)
        if stale:
            self.link_var.set("STALE · no telemetry for 500 ms")
            self.link_label.configure(fg=RED)
            self.note_var.set("Telemetry lost · virtual heater/pump display stopped.")
            self.log_button.configure(state="disabled")
            self._set_actuators_stopped()
            if self.last_pump and self.pump_active_since is not None:
                stale_limit = self.last_packet_at + 0.5
                self.pump_elapsed_seconds += max(0.0, stale_limit - self.pump_active_since)
                self.pump_active_since = None
                self.last_pump = False
            for led in ("ready", "heating", "fault", "heartbeat"):
                self._set_led(led, False)
        self.animation_phase = (self.animation_phase + 1) % 20
        self._draw_machine()
        self.root.after(80, self._animate)

    def _draw_machine(self) -> None:
        c = self.machine_canvas
        c.delete("machine")
        w = max(c.winfo_width(), 300)
        h = max(c.winfo_height(), 300)
        cx = w * 0.50
        scale = min(w / 680, h / 540)
        def xy(x: float, y: float) -> tuple[float, float]:
            return cx + (x - 340) * scale, (y - 270) * scale
        def rect(x1: float, y1: float, x2: float, y2: float, **kw: object) -> None:
            c.create_rectangle(*xy(x1, y1), *xy(x2, y2), tags="machine", **kw)
        def oval(x1: float, y1: float, x2: float, y2: float, **kw: object) -> None:
            c.create_oval(*xy(x1, y1), *xy(x2, y2), tags="machine", **kw)
        def line(points: list[tuple[float, float]], **kw: object) -> None:
            coords = [v for point in points for v in xy(*point)]
            c.create_line(*coords, tags="machine", **kw)
        def text(x: float, y: float, value: str, **kw: object) -> None:
            c.create_text(*xy(x, y), text=value, tags="machine", **kw)

        text(340, 34, "LIVE MACHINE MODEL", fill=MUTED, font=("Segoe UI", 9, "bold"))
        # Ground shadow and rounded-looking appliance body.
        oval(154, 475, 530, 510, fill="#0d151c", outline="")
        rect(190, 105, 490, 440, fill="#dbe4e5", outline="#f4f7f5", width=2)
        rect(210, 122, 470, 205, fill="#273945", outline="#344b59", width=1)
        rect(218, 132, 284, 190, fill="#162630", outline="#415461")
        text(251, 143, "WATER", fill=MUTED, font=("Segoe UI", 7, "bold"))
        # Reservoir level is illustrative only; no tank sensor is installed.
        rect(224, 154, 278, 184, fill="#174e5c", outline="")
        rect(224, 170, 278, 184, fill="#2da4b1", outline="")
        text(250, 161, "SIM", fill="#d8f8f7", font=("Segoe UI", 7, "bold"))
        # Status LEDs mirror the individual MCU output values.
        telemetry_fresh = self.connected and self.last_packet_at is not None and \
            time.monotonic() - self.last_packet_at <= 0.5
        for x, key, color in ((315, "ready_led", GREEN), (353, "heating_led", YELLOW),
                              (391, "fault_led", RED), (429, "heartbeat_led", TEAL)):
            oval(x - 9, 146, x + 9, 164,
                 fill=color if telemetry_fresh and self.data.get(key) == "1" else "#34444e", outline="")
        text(372, 181, self.data.get("state", "OFFLINE"), fill=TEXT,
             font=("Segoe UI", 10, "bold"))
        # Brew head and cup.
        rect(287, 205, 393, 231, fill="#b8c6ca", outline="#92a3a9")
        rect(322, 230, 358, 279, fill="#283b45", outline="#435862")
        rect(305, 280, 375, 292, fill="#101b22", outline="")
        pump = telemetry_fresh and self.data.get("pump") == "1"
        if pump:
            offset = (self.animation_phase % 8) * 2
            line([(340, 292), (340, 324 + offset)], fill="#a86938", width=max(2, int(4 * scale)))
            oval(335, 317 + offset, 345, 327 + offset, fill="#bd8250", outline="")
        # Cup body.
        rect(282, 338, 398, 421, fill="#f7f8f3", outline="#c8d2d2", width=2)
        line([(399, 350), (424, 350), (424, 399), (399, 399)], fill="#e3e9e8", width=5)
        brew_target = 32 if self.data.get("strength") == "STRONG" else 20
        fill_fraction = min(1.0, self.pump_elapsed_seconds / brew_target)
        if pump and self.pump_active_since is not None:
            fill_fraction = min(1.0, (self.pump_elapsed_seconds +
                                      time.monotonic() - self.pump_active_since) / brew_target)
        cup_top = 414 - (36 * fill_fraction)
        rect(291, cup_top, 389, 414,
             fill="#6a422b" if fill_fraction > 0 else "#eff2ef", outline="")
        text(340, 458, "VIRTUAL BREW OUTPUT", fill=MUTED,
             font=("Segoe UI", 8, "bold"))
        text(340, 492, "Heater / pump are simulated · no mains hardware",
             fill=YELLOW, font=("Segoe UI", 8))

    def close(self) -> None:
        self._disconnect()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    CoffeeTwin(root)
    root.mainloop()


if __name__ == "__main__":
    main()
