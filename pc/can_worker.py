"""
can_worker.py — Background CAN thread for the EIS Flask server.

Handles:
  - HW diagnostics: open bus, listen for STAT_READY (0x201/[0x00]) from MCU
  - Sending sweep config frames (SetStartHz, SetStopHz, SetPoints)
  - Sending START trigger
  - Receiving impedance data points and status frames
  - Emitting SocketIO events to the browser as data arrives
  - Saving results to pc/results/ on sweep complete

SocketIO events emitted:
  hw_check        {check: "vector"|"can_bus"|"mcufirmware", status: "checking"|"ok"|"error", message: "..."}
  sweep_started   {}
  data_point      {index, freq, re, im, total}
  sweep_complete  {points: [{freq, re, im}, ...], file: "path/to/saved.txt"}
  sweep_error     {message: "..."}
"""

import math
import os
import struct
import threading
import time
from datetime import datetime

import can
import json

# ── CAN configuration ────────────────────────────────────────────────────────
APP_NAME = "MCP_Integration"
CHANNEL  = 0          # Vector CH1 (index 0)
BITRATE  = 125_000    # 125 kbps

CAN_ID_CMD  = 0x100
CAN_ID_DATA = 0x200
CAN_ID_STAT = 0x201

CMD_SET_START  = 0x01
CMD_SET_STOP   = 0x02
CMD_SET_POINTS = 0x03
CMD_SET_AC_VOLT = 0x04
CMD_SET_DC_VOLT = 0x05
CMD_START      = 0x10

STAT_READY    = 0x00   # firmware heartbeat while IDLE — used for HW diagnostics
STAT_STARTED  = 0x01
STAT_COMPLETE = 0x02

# Timing
DIAG_TIMEOUT_S     = 5.0    # max wait for STAT_READY during diagnostics
CONFIG_GAP_S       = 0.05   # gap between config frames
RCAL_TIMEOUT_S     = 120.0  # generous: RCAL can take ~40 s at 1 Hz
PERPOINT_TIMEOUT_S = 15.0   # max wait between consecutive impedance frames
# ─────────────────────────────────────────────────────────────────────────────


def _log_freq(start_hz, stop_hz, n):
    """Logarithmically-spaced frequency list matching firmware sweep."""
    if n == 1:
        return [start_hz]
    step = math.log10(stop_hz / start_hz) / (n - 1)
    return [start_hz * (10 ** (step * i)) for i in range(n)]


def _next_eis_name(folder):
    """Find next available 'EIS Measurement' name in folder."""
    base = "EIS Measurement"
    if not os.path.exists(os.path.join(folder, f"{base}.eis")):
        return base
    i = 2
    while os.path.exists(os.path.join(folder, f"{base} ({i}).eis")):
        i += 1
    return f"{base} ({i})"


def _save_eis(points, config, folder):
    """Save sweep data as .eis JSON file. Returns (filepath, filename)."""
    os.makedirs(folder, exist_ok=True)
    name = _next_eis_name(folder)
    data = {
        "version": 1,
        "name": name,
        "timestamp": datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "config": config,
        "data": [{"freq": p["freq"], "re": p["re"], "im": p["im"]} for p in points],
    }
    filename = name + ".eis"
    filepath = os.path.join(folder, filename)
    with open(filepath, "w") as f:
        json.dump(data, f)
    return filepath, filename


class CANWorker:
    def __init__(self, socketio):
        self._socketio     = socketio
        self._thread       = None
        self._lock         = threading.Lock()
        self._diag_running = False
        self._stop_event   = threading.Event()

    @property
    def is_running(self):
        return self._thread is not None and self._thread.is_alive()

    # ── Diagnostics ───────────────────────────────────────────────────────────

    def run_diagnostics(self):
        """Launch HW diagnostics in background. No-op if sweep or diag in progress."""
        with self._lock:
            if self.is_running or self._diag_running:
                return
            self._diag_running = True
        t = threading.Thread(target=self._do_diagnostics, daemon=True, name="hw-diag")
        t.start()

    def _do_diagnostics(self):
        try:
            # ── Check 1: Vector VN1640A ────────────────────────────────────
            self._emit("hw_check", {
                "check": "vector", "status": "checking",
                "message": "Opening VN1640A..."
            })
            try:
                bus = can.Bus(
                    interface="vector",
                    channel=CHANNEL,
                    bitrate=BITRATE,
                    app_name=APP_NAME,
                    rx_queue_size=256,
                )
            except Exception as exc:
                self._emit("hw_check", {
                    "check": "vector", "status": "error",
                    "message": f"VN1640A open failed: {exc}"
                })
                return

            self._emit("hw_check", {
                "check": "vector", "status": "ok",
                "message": "VN1640A connected"
            })

            # ── Checks 2+3: CAN bus + MCU firmware ────────────────────────
            # Listen for STAT_READY (0x201/[0x00]) sent periodically by MCU while IDLE.
            # Receiving it confirms: CAN bus wired correctly + MCU firmware running.
            self._emit("hw_check", {
                "check": "can_bus", "status": "checking",
                "message": "Listening on CAN bus..."
            })
            self._emit("hw_check", {
                "check": "mcufirmware", "status": "checking",
                "message": "Waiting for MCU heartbeat (~1 s)..."
            })

            try:
                deadline = time.monotonic() + DIAG_TIMEOUT_S
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        self._emit("hw_check", {
                            "check": "can_bus", "status": "error",
                            "message": "No frames received — check CAN wiring"
                        })
                        self._emit("hw_check", {
                            "check": "mcufirmware", "status": "error",
                            "message": "No MCU heartbeat — check power / reset board"
                        })
                        return

                    frame = bus.recv(timeout=min(remaining, 0.5))
                    if frame is None:
                        continue
                    if frame.is_error_frame:
                        self._emit("hw_check", {
                            "check": "can_bus", "status": "error",
                            "message": "CAN error frame — check wiring / termination"
                        })
                        self._emit("hw_check", {
                            "check": "mcufirmware", "status": "error",
                            "message": "CAN bus error"
                        })
                        return

                    if (frame.arbitration_id == CAN_ID_STAT
                            and len(frame.data) >= 1
                            and frame.data[0] == STAT_READY):
                        self._emit("hw_check", {
                            "check": "can_bus", "status": "ok",
                            "message": "CAN bus OK"
                        })
                        self._emit("hw_check", {
                            "check": "mcufirmware", "status": "ok",
                            "message": "Firmware ready"
                        })
                        return
                    # Other frames (e.g. leftover data points) — keep listening
            finally:
                bus.shutdown()
        finally:
            with self._lock:
                self._diag_running = False

    # ── Sweep ─────────────────────────────────────────────────────────────────

    def start_sweep(self, start_hz, stop_hz, points, ac_volt_pp=300.0, dc_volt=1200.0,
                    save_folder=None):
        """Launch a background sweep thread. Returns (True, 'OK') or (False, reason)."""
        with self._lock:
            if self.is_running:
                return False, "A sweep is already in progress"
            if self._diag_running:
                return False, "Hardware check in progress — please wait"
            self._thread = threading.Thread(
                target=self._run,
                args=(float(start_hz), float(stop_hz), int(points),
                      float(ac_volt_pp), float(dc_volt), save_folder),
                daemon=True,
                name="can-worker",
            )
            self._stop_event.clear()
            self._thread.start()
        return True, "OK"

    def stop_sweep(self):
        """Signal the running sweep to stop."""
        self._stop_event.set()

    # ── Private helpers ───────────────────────────────────────────────────────

    def _emit(self, event, data=None):
        self._socketio.emit(event, data or {})

    def _run(self, start_hz, stop_hz, points, ac_volt_pp, dc_volt, save_folder):
        """Thread entry: open CAN bus, run sweep, close bus."""
        try:
            bus = can.Bus(
                interface="vector",
                channel=CHANNEL,
                bitrate=BITRATE,
                app_name=APP_NAME,
                rx_queue_size=256,
            )
        except Exception as exc:
            self._emit("sweep_error", {"message": f"CAN open failed: {exc}"})
            return

        try:
            self._do_sweep(bus, start_hz, stop_hz, points, ac_volt_pp, dc_volt, save_folder)
        except Exception as exc:
            self._emit("sweep_error", {"message": f"Unexpected error: {exc}"})
        finally:
            bus.shutdown()

    def _send_config(self, bus, start_hz, stop_hz, points, ac_volt_pp, dc_volt):
        """Send the five parameter config frames before START."""
        frames = [
            [CMD_SET_START]   + list(struct.pack("<f", start_hz)),
            [CMD_SET_STOP]    + list(struct.pack("<f", stop_hz)),
            [CMD_SET_POINTS]  + list(struct.pack("<H", points)),
            [CMD_SET_AC_VOLT] + list(struct.pack("<f", ac_volt_pp)),
            [CMD_SET_DC_VOLT] + list(struct.pack("<f", dc_volt)),
        ]
        for data in frames:
            bus.send(can.Message(
                arbitration_id=CAN_ID_CMD,
                data=data,
                is_extended_id=False,
            ))
            time.sleep(CONFIG_GAP_S)

    def _do_sweep(self, bus, start_hz, stop_hz, points, ac_volt_pp, dc_volt, save_folder):
        """Send config + START, then receive and stream impedance data."""
        config = {
            "start_hz": start_hz, "stop_hz": stop_hz, "points": points,
            "ac_volt_pp": ac_volt_pp, "dc_volt": dc_volt,
        }
        self._send_config(bus, start_hz, stop_hz, points, ac_volt_pp, dc_volt)
        bus.send(can.Message(
            arbitration_id=CAN_ID_CMD,
            data=[CMD_START],
            is_extended_id=False,
        ))

        freqs    = _log_freq(start_hz, stop_hz, points)
        pts      = []
        started  = False
        deadline = time.monotonic() + RCAL_TIMEOUT_S

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                if not started:
                    self._emit("sweep_error",
                               {"message": "Timeout: sweep start (0x201/0x01) never received"})
                    return
                if len(pts) >= points:
                    filepath, filename = _save_eis(pts, config, save_folder)
                    self._emit("sweep_complete", {
                        "points": pts, "file": filepath, "filename": filename, "config": config
                    })
                else:
                    self._emit("sweep_error",
                               {"message": f"Timeout after {len(pts)}/{points} points"})
                return

            if self._stop_event.is_set():
                if pts:
                    filepath, filename = _save_eis(pts, config, save_folder)
                    self._emit("sweep_stopped", {
                        "points": pts, "file": filepath, "filename": filename,
                        "config": config, "message": f"Stopped after {len(pts)}/{points} points"
                    })
                else:
                    self._emit("sweep_stopped", {"message": "Sweep stopped"})
                return

            frame = bus.recv(timeout=min(remaining, 1.0))
            if frame is None:
                continue
            if frame.is_error_frame:
                continue

            # ── Status frame ──────────────────────────────────────────────
            if frame.arbitration_id == CAN_ID_STAT and len(frame.data) >= 1:
                status = frame.data[0]
                if status == STAT_STARTED:
                    started = True
                    deadline = time.monotonic() + RCAL_TIMEOUT_S
                    self._emit("sweep_started")
                elif status == STAT_COMPLETE:
                    filepath, filename = _save_eis(pts, config, save_folder)
                    self._emit("sweep_complete", {
                        "points": pts, "file": filepath, "filename": filename, "config": config
                    })
                    return
                # STAT_READY (0x00) during sweep is ignored
                continue

            # ── Impedance data frame ───────────────────────────────────────
            if frame.arbitration_id == CAN_ID_DATA and len(frame.data) == 8:
                re, im = struct.unpack("<ff", bytes(frame.data[:8]))
                idx    = len(pts)
                freq   = freqs[idx] if idx < len(freqs) else 0.0
                pt     = {"index": idx, "freq": round(freq, 4),
                          "re": round(re, 4), "im": round(im, 4)}
                pts.append(pt)
                self._emit("data_point", {**pt, "total": points})
                deadline = time.monotonic() + PERPOINT_TIMEOUT_S
                if len(pts) >= points:
                    filepath, filename = _save_eis(pts, config, save_folder)
                    self._emit("sweep_complete", {
                        "points": pts, "file": filepath, "filename": filename, "config": config
                    })
                    return
