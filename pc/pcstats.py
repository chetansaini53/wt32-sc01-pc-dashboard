"""
PC stats -> WT32-SC01 Plus over USB serial.
Reads CPU / RAM / GPU / Disk / Net and streams them to the display once a second.

Setup:  pip install psutil      (pyserial already present; GPU via nvidia-smi)
Run:    python pcstats.py            (auto-detects the COM port)
        python pcstats.py COM3       (force a port)
"""
import sys, time, socket, subprocess
import psutil
import serial, serial.tools.list_ports

BAUD = 115200
GB = 1024 ** 3


def find_port(forced):
    if forced:
        return forced
    for p in serial.tools.list_ports.comports():
        d = (p.description or "").lower()
        if any(k in d for k in ("ch340", "ch343", "cp210", "cp21", "usb-serial", "usb serial", "uart", "esp")):
            return p.device
    ports = list(serial.tools.list_ports.comports())
    return ports[0].device if ports else None


def gpu_stats():
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu",
             "--format=csv,noheader,nounits"],
            stderr=subprocess.DEVNULL, timeout=2).decode().strip().splitlines()[0]
        u, mu, mt, t = [x.strip() for x in out.split(",")]
        return int(float(u)), round(float(mu) / 1024, 1), round(float(mt) / 1024, 1), int(float(t))
    except Exception:
        return 0, 0.0, 0.0, 0


def cpu_temp():
    """Best-effort CPU temp on Windows. ACPI thermal zone first (no extra SW),
    then LibreHardwareMonitor / OpenHardwareMonitor if they are running. 0 = N/A.
    Cached for 3s so we don't spawn PowerShell every second."""
    now = time.time()
    if now - cpu_temp._t < 3:
        return cpu_temp._c
    cpu_temp._t = now
    ps = ["powershell", "-NoProfile", "-Command"]
    try:
        out = subprocess.check_output(
            ps + ["(Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction Stop | Select-Object -First 1 -ExpandProperty CurrentTemperature)"],
            stderr=subprocess.DEVNULL, timeout=4).decode().strip()
        if out:
            c = int(float(out)) / 10.0 - 273.15
            if 15 < c < 120:
                cpu_temp._c = int(round(c)); return cpu_temp._c
    except Exception:
        pass
    for ns in ("root/LibreHardwareMonitor", "root/OpenHardwareMonitor"):
        try:
            out = subprocess.check_output(
                ps + ["Get-CimInstance -Namespace " + ns + " -ClassName Sensor -ErrorAction Stop | "
                      "Where-Object {$_.SensorType -eq 'Temperature' -and $_.Name -like '*CPU*'} | "
                      "Select-Object -First 1 -ExpandProperty Value"],
                stderr=subprocess.DEVNULL, timeout=4).decode().strip()
            if out:
                cpu_temp._c = int(round(float(out.splitlines()[0]))); return cpu_temp._c
        except Exception:
            pass
    cpu_temp._c = 0
    return 0
cpu_temp._t = 0
cpu_temp._c = 0


def main():
    forced = sys.argv[1] if len(sys.argv) > 1 else None
    port = find_port(forced)
    if not port:
        print("No serial port found."); return
    try:
        ser = serial.Serial(port, BAUD, timeout=0.2); time.sleep(2.0)
    except Exception as e:
        print("Serial open failed:", e); return
    print("Streaming PC stats ->", port, "  (Ctrl+C to stop)")

    host = socket.gethostname()[:14]
    cores = psutil.cpu_count()
    last = psutil.net_io_counters(); lt = time.time()
    lastdio = psutil.disk_io_counters()
    psutil.cpu_percent()  # prime

    try:
        while True:
            cpu = int(psutil.cpu_percent(interval=1.0))
            vm = psutil.virtual_memory()
            ram, ru, rt = int(vm.percent), round(vm.used / GB, 1), round(vm.total / GB, 1)
            dk = psutil.disk_usage("C:\\")
            dused, dtot = round(dk.used / GB), round(dk.total / GB)
            g, gu, gtot, gtemp = gpu_stats()
            ctp = cpu_temp()
            now = psutil.net_io_counters(); dio = psutil.disk_io_counters(); t = time.time(); dt = max(t - lt, 0.001)
            umb = (now.bytes_sent - last.bytes_sent) / dt / 1024
            dmb = (now.bytes_recv - last.bytes_recv) / dt / 1024
            busy = (dio.read_time + dio.write_time) - (lastdio.read_time + lastdio.write_time)
            disk = int(max(0, min(100, busy / (dt * 1000) * 100)))
            upt = int(time.time() - psutil.boot_time())
            last, lt, lastdio = now, t, dio
            line = (f"CPU:{cpu};RAM:{ram};RU:{ru};RT:{rt};GPU:{g};GU:{gu};GT:{gtot};GTP:{gtemp};CTP:{ctp};"
                    f"DSK:{disk};DU:{dused};DT:{dtot};DKB:{dmb:.1f};UKB:{umb:.1f};UPT:{upt};CORES:{cores};HOST:{host}\n")
            ser.write(line.encode())
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print("stopped:", e)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
