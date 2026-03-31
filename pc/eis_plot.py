#!/usr/bin/env python3
"""
EIS Nyquist Plotter — MCP Integration Project
Parses UART output from ADICUP3029 + EVAL-AD5941BATZ and generates:
  - nyquist.png   : static Nyquist plot
  - data.json     : structured data + full test metadata
  - report.html   : self-contained HTML report (embedded plot + setup tables)

Usage:
    python eis_plot.py [data_file.txt]   # read from file
    python eis_plot.py                    # paste UART output, end with Ctrl+Z (Windows)

Output goes to:  pc/results/test_YYYYMMDD_HHMMSS/
"""

import base64
import io
import json
import os
import re
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")   # no GUI needed
import matplotlib.pyplot as plt
import numpy as np


# ── Test setup metadata ──────────────────────────────────────────────────────
# Hardware and firmware settings are fixed for this sprint.
# Update "notes" to describe the DUT / test conditions for each run.
SETUP = {
    "hardware": {
        "mcu":           "EVAL-ADICUP3029 (ADuCM3029 ARM Cortex-M3, 3.3 V)",
        "eis_board":     "EVAL-AD5941BATZ (AD5941 impedance analyzer)",
        "can_interface": "Vector VN1640A — CH1, 125 kbps",
        "mcp2515_board": "Joy-IT SBC-CAN01 (MCP2515 + TJA1050, 16 MHz crystal, P1 term ON)",
        "uart":          "115200 baud (auto-detected COM port)",
        "daplink":       "DAPLINK USB drive (drag-and-drop .hex flash)",
    },
    "firmware": {
        "dft_num":              1024,
        "sweep_start_hz":       1.0,
        "sweep_stop_hz":        1000.0,
        "sweep_points":         20,
        "sweep_log":            True,
        "ac_excitation_mv_pp":  800,
        "dc_bias_mv":           1100,
        "rcal_mohm":            50,
        "spi0_div":             2,
        "spi0_clock_mhz":       4.3,
        "spi1_clock_mhz":       "~3.25 (MCP2515)",
        "power_mode":           "LP (Low Power)",
        "adc_sinc3_osr":        4,
        "adc_sinc2_osr":        "auto (frequency-dependent)",
        "can_baud_kbps":        125,
        "mcp2515_cnf1":         "0x03",
        "mcp2515_cnf2":         "0xB5",
        "mcp2515_cnf3":         "0x01",
    },
    "accuracy_note": (
        "DftNum=1024 compromise: ~3.3 s/pt at 1 Hz (~3 cycles captured). "
        "Adequate for hardware verification. "
        "For production accuracy at low frequencies, increase DftNum to 4096–8192."
    ),
    "notes": "",   # <-- describe the DUT and test conditions here before running
}
# ─────────────────────────────────────────────────────────────────────────────


def parse_data(text):
    """Parse lines: 'Freq:X.XXX Re=X.XX Im=X.XX mOhm'"""
    pattern = r"Freq:([\d.]+)\s+Re=([\d.]+)\s+Im=([\d.]+)\s+mOhm"
    points = []
    for m in re.finditer(pattern, text):
        points.append({
            "freq_hz":  float(m.group(1)),
            "re_mohm":  float(m.group(2)),
            "im_mohm":  float(m.group(3)),
        })
    return points


def make_plot(points, timestamp):
    freq = np.array([p["freq_hz"] for p in points])
    re   = np.array([p["re_mohm"] for p in points])
    im   = np.array([p["im_mohm"] for p in points])

    fig, ax = plt.subplots(figsize=(8, 6))
    fig.patch.set_facecolor("#f8f9fa")
    ax.set_facecolor("#ffffff")

    # Line connecting points
    ax.plot(re, im, color="#bbbbbb", linewidth=1.2, zorder=2)

    # Scatter coloured by log frequency (low=purple, high=yellow)
    sc = ax.scatter(re, im,
                    c=np.log10(freq), cmap="plasma",
                    s=55, zorder=3, edgecolors="white", linewidths=0.5)

    # Annotate lowest and highest frequency
    for idx, label in [(0, f"{freq[0]:.1f} Hz"), (-1, f"{freq[-1]:.0f} Hz")]:
        ax.annotate(label, (re[idx], im[idx]),
                    textcoords="offset points", xytext=(7, 5),
                    fontsize=8.5, color="#333333",
                    arrowprops=dict(arrowstyle="-", color="#aaaaaa", lw=0.8))

    cbar = fig.colorbar(sc, ax=ax, pad=0.02)
    cbar.set_label("log₁₀(f / Hz)", fontsize=9)
    cbar.ax.tick_params(labelsize=8)

    fw = SETUP["firmware"]
    ax.set_xlabel("Re(Z)  [mΩ]", fontsize=11)
    ax.set_ylabel("−Im(Z)  [mΩ]", fontsize=11)
    ax.set_title(
        f"EIS Nyquist Plot — EVAL-AD5941BATZ\n"
        f"{fw['sweep_start_hz']:.0f} Hz – {fw['sweep_stop_hz']:.0f} Hz  |  "
        f"DftNum {fw['dft_num']}  |  {fw['sweep_points']} pts log  |  {timestamp}",
        fontsize=10.5, pad=10
    )

    ax.grid(True, linestyle="--", alpha=0.45, color="#cccccc")
    ax.tick_params(labelsize=9)

    # Ensure axes start near the data
    ax.set_xlim(left=max(0, re.min() - 2))
    ax.set_ylim(bottom=0)

    plt.tight_layout()
    return fig


def fig_to_base64(fig):
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=150, bbox_inches="tight")
    buf.seek(0)
    return base64.b64encode(buf.read()).decode()


def make_html(points, b64_png, timestamp, out_dir):
    fw = SETUP["firmware"]
    hw = SETUP["hardware"]

    data_rows = "\n".join(
        f'<tr><td>{p["freq_hz"]:.3f}</td>'
        f'<td>{p["re_mohm"]:.2f}</td>'
        f'<td>{p["im_mohm"]:.2f}</td></tr>'
        for p in points
    )
    hw_rows = "\n".join(
        f"<tr><td>{k.replace('_', ' ').title()}</td><td>{v}</td></tr>"
        for k, v in hw.items()
    )
    fw_rows = "\n".join(
        f"<tr><td>{k.replace('_', ' ').title()}</td><td>{v}</td></tr>"
        for k, v in fw.items()
    )

    notes_html = SETUP["notes"] if SETUP["notes"] else "<em>No notes recorded for this test.</em>"
    accuracy_html = SETUP["accuracy_note"]

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EIS Report — {timestamp}</title>
<style>
  *, *::before, *::after {{ box-sizing: border-box; }}
  body  {{ font-family: 'Segoe UI', Arial, sans-serif; background: #eef1f6;
           color: #1e2030; margin: 0; padding: 28px 20px; }}
  h1    {{ color: #1a3a5c; margin: 0 0 4px; font-size: 1.7em; }}
  .meta {{ color: #5a6070; font-size: 0.88em; margin-bottom: 28px; }}
  h2    {{ color: #1a3a5c; font-size: 1.1em; margin: 0 0 14px;
           border-bottom: 2px solid #2c6fad; padding-bottom: 5px; }}
  .card {{ background: #fff; border-radius: 10px;
           box-shadow: 0 2px 10px rgba(0,0,0,0.09);
           padding: 22px 26px; margin-bottom: 22px; }}
  .plot-img {{ max-width: 100%; border-radius: 6px; display: block; margin: auto; }}
  table   {{ border-collapse: collapse; width: 100%; font-size: 0.875em; }}
  th      {{ background: #1a3a5c; color: #fff; padding: 8px 12px; text-align: left;
             font-weight: 600; }}
  td      {{ padding: 6px 12px; border-bottom: 1px solid #e4e8ef; }}
  tr:nth-child(even) td {{ background: #f3f6fb; }}
  .grid   {{ display: grid; grid-template-columns: 1fr 1fr; gap: 22px; }}
  .badge  {{ display: inline-block; background: #2c6fad; color: #fff;
             border-radius: 4px; padding: 2px 8px; font-size: 0.8em;
             margin-right: 6px; font-weight: 600; }}
  .warn   {{ background: #fff8e1; border-left: 4px solid #ffb300;
             padding: 10px 14px; border-radius: 4px; margin-top: 14px;
             font-size: 0.85em; color: #5a4000; }}
  .notes-box {{ background: #f0f7ff; border-left: 4px solid #2c6fad;
               padding: 12px 16px; border-radius: 4px; font-size: 0.92em; }}
  @media (max-width: 680px) {{ .grid {{ grid-template-columns: 1fr; }} }}
</style>
</head>
<body>

<h1>EIS Impedance Report</h1>
<p class="meta">
  <span class="badge">Sprint 5A</span>
  Generated: <strong>{timestamp}</strong> &nbsp;|&nbsp;
  Output directory: <code>{os.path.basename(out_dir)}</code>
</p>

<div class="card">
  <h2>Nyquist Plot</h2>
  <img class="plot-img" src="data:image/png;base64,{b64_png}" alt="Nyquist plot">
</div>

<div class="card">
  <h2>Measurement Data ({len(points)} points)</h2>
  <table>
    <tr><th>Freq (Hz)</th><th>Re(Z) (mΩ)</th><th>−Im(Z) (mΩ)</th></tr>
    {data_rows}
  </table>
</div>

<div class="grid">
  <div class="card">
    <h2>Hardware</h2>
    <table>
      <tr><th>Parameter</th><th>Value</th></tr>
      {hw_rows}
    </table>
  </div>
  <div class="card">
    <h2>Firmware Settings</h2>
    <table>
      <tr><th>Parameter</th><th>Value</th></tr>
      {fw_rows}
    </table>
    <div class="warn">&#9888; {accuracy_html}</div>
  </div>
</div>

<div class="card">
  <h2>Test Notes</h2>
  <div class="notes-box">{notes_html}</div>
</div>

</body>
</html>"""
    return html


def main():
    # ── Read input ───────────────────────────────────────────────────────────
    if len(sys.argv) > 1:
        with open(sys.argv[1], "r") as f:
            raw = f.read()
        print(f"Reading from: {sys.argv[1]}")
    else:
        print("Paste UART output below, then press Ctrl+Z + Enter (Windows) or Ctrl+D (Linux):")
        raw = sys.stdin.read()

    points = parse_data(raw)
    if not points:
        print("ERROR: no data points found. Expected format: 'Freq:X.XXX Re=X.XX Im=X.XX mOhm'")
        sys.exit(1)
    print(f"Parsed {len(points)} data points.")

    # ── Output directory ─────────────────────────────────────────────────────
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    fw = SETUP["firmware"]
    start = fw["sweep_start_hz"]
    stop  = fw["sweep_stop_hz"]
    pts   = fw["sweep_points"]
    start_str = f"{int(start)}Hz"
    stop_str  = f"{int(stop/1000)}kHz" if stop >= 1000 else f"{int(stop)}Hz"
    folder = f"Sweep_{start_str}_{stop_str}_{pts}pts_{timestamp}"
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.join(script_dir, "results", folder)
    os.makedirs(out_dir, exist_ok=True)

    # ── Save raw data ────────────────────────────────────────────────────────
    with open(os.path.join(out_dir, "raw_data.txt"), "w") as f:
        f.write(raw)

    # ── Save JSON record ─────────────────────────────────────────────────────
    record = {"timestamp": timestamp, "setup": SETUP, "data": points}
    json_path = os.path.join(out_dir, "data.json")
    with open(json_path, "w") as f:
        json.dump(record, f, indent=2)

    # ── Plot ─────────────────────────────────────────────────────────────────
    fig = make_plot(points, timestamp)
    png_path = os.path.join(out_dir, "nyquist.png")
    fig.savefig(png_path, dpi=150, bbox_inches="tight")
    b64_png = fig_to_base64(fig)
    plt.close(fig)

    # ── HTML report ──────────────────────────────────────────────────────────
    html = make_html(points, b64_png, timestamp, out_dir)
    html_path = os.path.join(out_dir, "report.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html)

    # ── Summary ──────────────────────────────────────────────────────────────
    print(f"\nOutputs saved to: {out_dir}")
    print(f"  nyquist.png  — static plot")
    print(f"  data.json    — structured data + full metadata")
    print(f"  report.html  — open in browser for full report")
    print(f"  raw_data.txt — original UART output")


if __name__ == "__main__":
    main()
