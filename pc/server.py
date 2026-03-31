"""
server.py — Sprint 7: Flask + SocketIO backend for browser EIS interface.

Start:
    python pc/server.py
    Open: http://localhost:8080

SocketIO events (browser → server):
    start_sweep   {start_hz, stop_hz, points}  — configure + trigger sweep
    stop_sweep    {}                            — (placeholder, future use)

SocketIO events (server → browser):
    sweep_started   {}
    data_point      {index, freq, re, im, total}
    sweep_complete  {points: [{freq, re, im}, ...], file: "path"}
    sweep_error     {message: "..."}
    status          {running: bool}
"""

import json as _json
import os

from flask import Flask, jsonify, render_template, request
from flask_socketio import SocketIO

from can_worker import CANWorker

# ── Measurement folder config ────────────────────────────────────────────────
_CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".eis_config.json")
_DEFAULT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "measurements")

def _load_folder():
    """Return the configured measurement folder, creating default if needed."""
    if os.path.exists(_CONFIG_FILE):
        try:
            with open(_CONFIG_FILE, "r") as f:
                cfg = _json.load(f)
                folder = cfg.get("folder", _DEFAULT_FOLDER)
                if os.path.isdir(folder):
                    return folder
        except Exception:
            pass
    os.makedirs(_DEFAULT_FOLDER, exist_ok=True)
    return _DEFAULT_FOLDER

def _save_folder(folder):
    """Persist the measurement folder path."""
    with open(_CONFIG_FILE, "w") as f:
        _json.dump({"folder": folder}, f)

measurement_folder = _load_folder()

# ── App setup ─────────────────────────────────────────────────────────────────
app = Flask(__name__, template_folder="templates", static_folder="static")
app.config["SECRET_KEY"] = "eis-mcp-integration"
app.config["TEMPLATES_AUTO_RELOAD"] = True

# threading async_mode: no eventlet/gevent required
socketio = SocketIO(app, async_mode="threading", cors_allowed_origins="*")
worker   = CANWorker(socketio)
# ─────────────────────────────────────────────────────────────────────────────


# ── HTTP routes ───────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/status")
def status():
    return jsonify({"running": worker.is_running})


@app.route("/api/folder", methods=["GET"])
def get_folder():
    global measurement_folder
    return jsonify({"folder": measurement_folder})

@app.route("/api/browse-folder", methods=["POST"])
def browse_folder():
    """Open a native Windows folder picker via SHBrowseForFolderW (pure ctypes)."""
    import threading as _th
    result = [None]

    def _pick():
        import ctypes
        from ctypes import wintypes
        shell32 = ctypes.windll.shell32
        ole32 = ctypes.windll.ole32
        ole32.CoInitialize(None)
        try:
            BIF_RETURNONLYFSDIRS = 0x0001
            BIF_NEWDIALOGSTYLE = 0x0040
            BIF_EDITBOX = 0x0010
            MAX_PATH = 260

            class BROWSEINFOW(ctypes.Structure):
                _fields_ = [
                    ("hwndOwner", wintypes.HWND),
                    ("pidlRoot", ctypes.c_void_p),
                    ("pszDisplayName", ctypes.c_wchar_p),
                    ("lpszTitle", ctypes.c_wchar_p),
                    ("ulFlags", ctypes.c_uint),
                    ("lpfn", ctypes.c_void_p),
                    ("lParam", ctypes.c_long),
                    ("iImage", ctypes.c_int),
                ]

            buf = ctypes.create_unicode_buffer(MAX_PATH)
            bi = BROWSEINFOW()
            bi.hwndOwner = None
            bi.lpszTitle = "Select Measurement Folder"
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX
            bi.pszDisplayName = ctypes.cast(buf, ctypes.c_wchar_p)

            pidl = shell32.SHBrowseForFolderW(ctypes.byref(bi))
            if pidl:
                path_buf = ctypes.create_unicode_buffer(MAX_PATH)
                if shell32.SHGetPathFromIDListW(pidl, path_buf):
                    result[0] = path_buf.value
                ctypes.windll.ole32.CoTaskMemFree(pidl)
        finally:
            ole32.CoUninitialize()

    t = _th.Thread(target=_pick)
    t.start()
    t.join(timeout=120)
    if not result[0]:
        return jsonify({"cancelled": True})
    return jsonify({"folder": result[0]})

@app.route("/api/folder", methods=["POST"])
def set_folder():
    global measurement_folder
    data = request.get_json()
    folder = data.get("folder", "").strip()
    if not folder:
        return jsonify({"error": "Folder path required"}), 400
    if not os.path.isdir(folder):
        try:
            os.makedirs(folder, exist_ok=True)
        except OSError as exc:
            return jsonify({"error": f"Cannot create folder: {exc}"}), 400
    measurement_folder = folder
    _save_folder(folder)
    return jsonify({"folder": measurement_folder})

@app.route("/api/measurements")
def list_measurements():
    global measurement_folder
    files = []
    if os.path.isdir(measurement_folder):
        for fn in sorted(os.listdir(measurement_folder)):
            if fn.endswith(".eis"):
                fp = os.path.join(measurement_folder, fn)
                try:
                    with open(fp, "r") as f:
                        meta = _json.load(f)
                    files.append({
                        "filename": fn,
                        "name": meta.get("name", fn),
                        "timestamp": meta.get("timestamp", ""),
                        "config": meta.get("config", {}),
                        "point_count": len(meta.get("data", [])),
                    })
                except Exception:
                    pass
    files.sort(key=lambda f: f.get("timestamp", ""), reverse=True)
    return jsonify({"folder": measurement_folder, "files": files})

@app.route("/api/measurement/<path:filename>")
def get_measurement(filename):
    global measurement_folder
    fp = os.path.join(measurement_folder, filename)
    if not fp.endswith(".eis") or not os.path.isfile(fp):
        return jsonify({"error": "File not found"}), 404
    with open(fp, "r") as f:
        data = _json.load(f)
    return jsonify(data)

@app.route("/api/measurement/<path:filename>/rename", methods=["POST"])
def rename_measurement(filename):
    global measurement_folder
    fp = os.path.join(measurement_folder, filename)
    if not fp.endswith(".eis") or not os.path.isfile(fp):
        return jsonify({"error": "File not found"}), 404
    data = request.get_json()
    new_name = data.get("name", "").strip()
    if not new_name:
        return jsonify({"error": "Name required"}), 400

    with open(fp, "r") as f:
        content = _json.load(f)
    content["name"] = new_name

    # Rename the file too (sanitize)
    safe = "".join(c for c in new_name if c.isalnum() or c in " _-()").strip()
    if not safe:
        safe = "measurement"
    new_filename = safe + ".eis"
    new_fp = os.path.join(measurement_folder, new_filename)
    # Avoid overwriting
    if new_fp != fp and os.path.exists(new_fp):
        i = 2
        while os.path.exists(os.path.join(measurement_folder, f"{safe} ({i}).eis")):
            i += 1
        new_filename = f"{safe} ({i}).eis"
        new_fp = os.path.join(measurement_folder, new_filename)

    with open(new_fp, "w") as f:
        _json.dump(content, f)
    if new_fp != fp:
        os.remove(fp)
    return jsonify({"filename": new_filename, "name": new_name})


# ── SocketIO events ───────────────────────────────────────────────────────────

@socketio.on("connect")
def on_connect():
    socketio.emit("status", {"running": worker.is_running})
    worker.run_diagnostics()


@socketio.on("run_diagnostics")
def on_run_diagnostics():
    worker.run_diagnostics()


@socketio.on("start_sweep")
def on_start_sweep(data):
    try:
        start_hz   = float(data.get("start_hz", 1.0))
        stop_hz    = float(data.get("stop_hz", 1000.0))
        points     = int(data.get("points", 40))
        ac_volt_pp = float(data.get("ac_volt_pp", 300.0))
        dc_volt    = float(data.get("dc_volt", 1200.0))
    except (TypeError, ValueError) as exc:
        socketio.emit("sweep_error", {"message": f"Bad parameters: {exc}"})
        return

    if start_hz <= 0 or stop_hz <= start_hz:
        socketio.emit("sweep_error",
                      {"message": "start_hz must be > 0 and stop_hz must be > start_hz"})
        return
    if points < 1 or points > 100:
        socketio.emit("sweep_error", {"message": "points must be between 1 and 100"})
        return
    if ac_volt_pp < 10 or ac_volt_pp > 800:
        socketio.emit("sweep_error", {"message": "AC amplitude must be 10–800 mV"})
        return
    if dc_volt < 0 or dc_volt > 2400:
        socketio.emit("sweep_error", {"message": "DC bias must be 0–2400 mV"})
        return

    ok, msg = worker.start_sweep(start_hz, stop_hz, points, ac_volt_pp, dc_volt,
                                  save_folder=measurement_folder)
    if not ok:
        socketio.emit("sweep_error", {"message": msg})


@socketio.on("stop_sweep")
def on_stop_sweep():
    worker.stop_sweep()


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("=" * 55)
    print("  EIS Browser Server — Sprint 12")
    print("  Open: http://localhost:8080")
    print("  Ctrl+C to stop")
    print("=" * 55)
    socketio.run(app, host="0.0.0.0", port=8080, debug=False, use_reloader=False)
