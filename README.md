# WT32-SC01 PC Dashboard

A live **PC system monitor** on a **WT32-SC01** (ESP32 + 3.5" ST7796 480×320) — CPU, RAM, GPU, disk, network, temps and uptime, streamed from your PC over USB serial. Tap the screen to flip through pages, including a live CPU/GPU history graph.

![demo](assets/demo.jpg)

## Features

- 📊 **CPU / RAM / GPU / DISK** with coloured bars (green → yellow → red by load)
- 🌡️ **GPU & CPU temperatures**
- 🌐 **Network** up/down in KB/s · **Uptime**
- 📈 **Live graphs page** — scrolling CPU + GPU history (area + line)
- 🔴 **ALERT** indicator when something runs hot/high
- 👆 **5 pages**, tap the touch screen to cycle: `Dashboard · GPU · Disk · System · Graphs`
- 🔌 **USB serial** — no WiFi, no network setup

## How it works

```
PC (Python: psutil + nvidia-smi)  --USB serial-->  WT32-SC01 (LovyanGFX UI)
         CPU:34;RAM:61;GPU:12;GTP:54;...
```

A small Python script samples your PC once a second and sends a compact `key:value;…` line over the COM port. The ESP32 parses it and draws the dashboard.

## Hardware

- **WT32-SC01** (original: ESP32 + ST7796 over SPI + FT6336 touch). USB-C only — nothing to wire.
- Other ST7796/ILI9488 boards work too — just edit the `LGFX` config block at the top of the sketch (SPI pins / rotation / invert).

> The display config in the sketch is for the **original WT32-SC01**. The **SC01 Plus** (ESP32-S3, 8-bit parallel) needs a different `LGFX` bus config and the `esp32s3` board.

## 1. PC side (Python)

Requires **Python 3.8+**.

```bash
pip install psutil pyserial
```

- **GPU stats** come from `nvidia-smi` (installed with the NVIDIA driver) — NVIDIA GPUs only. On AMD/Intel the GPU page just reads 0.
- **CPU temperature** on Windows is best-effort: the script tries the ACPI thermal zone first. If it shows `N/A`, run **[LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)** (free) in the background — the script reads CPU temp from it automatically.

Run it (auto-detects the port, or pass one):

```bash
python pc/pcstats.py            # auto-detect
python pc/pcstats.py COM3       # force a port
```

## 2. ESP32 side (Arduino)

1. Install the **ESP32 board package** (Boards Manager → “esp32” by Espressif).
2. Install the **LovyanGFX** library (Library Manager).
3. Flash `firmware/pcstats_sc01`:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install LovyanGFX
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pcstats_sc01
arduino-cli upload  -p COM3 --fqbn esp32:esp32:esp32 firmware/pcstats_sc01
```

(replace `COM3` with your port). The screen shows **“Waiting for PC…”** until you start the Python script.

> Only one program can hold the COM port at a time — stop the Python script before flashing, then start it again afterwards.

## Pages (tap to change)

| Page | Shows |
|------|-------|
| 1 Dashboard | CPU + RAM bars, Download/Upload KB/s, Uptime, CPU/GPU temps |
| 2 GPU | load, temperature, VRAM used/total |
| 3 Disk | active %, C: space used/total, capacity |
| 4 System | hostname, cores, total RAM/disk, uptime |
| 5 Graphs | live CPU + GPU history (% + temp) |

## Notes

- Windows-oriented (uses `nvidia-smi`, PowerShell for CPU temp, disk `C:`). Linux/macOS would need small tweaks to `pcstats.py`.
- Disk % is **activity** (like Task Manager), not space; space is shown separately.

## License

MIT — see [LICENSE](LICENSE).
