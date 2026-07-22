import glob
import grp
import os
import pwd
import threading
import time
from typing import Optional

import serial
import serial.tools.list_ports
from fastapi import FastAPI
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel


BAUD = 115200
DEFAULT_MS = 300


class SerialBridge:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.ser: Optional[serial.Serial] = None
        self.port: Optional[str] = None

    def list_ports(self) -> list[str]:
        patterns = [
            "/dev/serial/by-id/*",
            "/dev/ttyACM*",
            "/dev/ttyUSB*",
            "/dev/cu.usbmodem*",
        ]
        ports: list[str] = []
        for pattern in patterns:
            ports.extend(glob.glob(pattern))
        return sorted(set(ports))

    def diagnostics(self) -> dict:
        user = pwd.getpwuid(os.getuid()).pw_name
        groups = [grp.getgrgid(gid).gr_name for gid in os.getgroups()]
        ports = []
        by_device = {p.device: p for p in serial.tools.list_ports.comports()}

        for port in self.list_ports():
            real = os.path.realpath(port)
            stat_result = os.stat(real)
            mode = oct(stat_result.st_mode & 0o777)
            owner = pwd.getpwuid(stat_result.st_uid).pw_name
            group = grp.getgrgid(stat_result.st_gid).gr_name
            can_read = os.access(port, os.R_OK)
            can_write = os.access(port, os.W_OK)
            info = by_device.get(real) or by_device.get(port)
            ports.append({
                "port": port,
                "real": real,
                "description": info.description if info else "",
                "hwid": info.hwid if info else "",
                "mode": mode,
                "owner": owner,
                "group": group,
                "can_read": can_read,
                "can_write": can_write,
            })

        return {
            "user": user,
            "groups": groups,
            "dialout": "dialout" in groups,
            "connected": self.port,
            "ports": ports,
        }

    def connect(self, port: Optional[str] = None) -> dict:
        with self.lock:
            if self.ser and self.ser.is_open:
                self.ser.close()

            chosen = port
            if not chosen:
                ports = self.list_ports()
                chosen = ports[0] if ports else None

            if not chosen:
                raise RuntimeError("No serial ports found. Plug in the XIAO or pass a port.")

            try:
                self.ser = serial.Serial(chosen, BAUD, timeout=0.25, write_timeout=0.25)
            except serial.SerialException as exc:
                if "Permission denied" in str(exc):
                    raise RuntimeError(
                        f"Permission denied opening {chosen}. On the Jetson run: "
                        "sudo usermod -aG dialout $USER && sudo reboot"
                    ) from exc
                raise

            self.port = chosen
            time.sleep(1.6)
            boot_lines = self._drain_locked()
            return {"port": chosen, "baud": BAUD, "boot_lines": boot_lines}

    def close(self) -> None:
        with self.lock:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.ser = None
            self.port = None

    def _require_locked(self) -> serial.Serial:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Not connected. Pick the XIAO serial port and press Connect first.")
        assert self.ser is not None
        return self.ser

    def _drain_locked(self) -> list[str]:
        assert self.ser is not None
        lines: list[str] = []
        end = time.time() + 0.35
        while time.time() < end:
            raw = self.ser.readline()
            if raw:
                lines.append(raw.decode(errors="replace").strip())
            else:
                time.sleep(0.02)
        return [line for line in lines if line]

    def command(self, text: str, wait_s: float = 0.35) -> dict:
        text = text.strip()
        if not text:
            raise RuntimeError("Empty command")

        with self.lock:
            ser = self._require_locked()
            self._drain_locked()
            ser.write((text + "\n").encode())
            ser.flush()

            lines: list[str] = []
            end = time.time() + wait_s
            while time.time() < end:
                raw = ser.readline()
                if raw:
                    lines.append(raw.decode(errors="replace").strip())
                else:
                    time.sleep(0.02)

            return {"command": text, "port": self.port, "lines": [line for line in lines if line]}


class ConnectRequest(BaseModel):
    port: Optional[str] = None


class CommandRequest(BaseModel):
    command: str
    wait_s: float = 0.35


bridge = SerialBridge()
app = FastAPI(title="OpenBot Serial GUI")


HTML = r"""
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>OpenBot Control</title>
  <style>
    body { margin: 0; font-family: system-ui, -apple-system, Segoe UI, sans-serif; background: #f6f7f9; color: #171717; }
    main { max-width: 980px; margin: 0 auto; padding: 20px; }
    h1 { font-size: 22px; margin: 0 0 14px; }
    section { background: white; border: 1px solid #ddd; border-radius: 8px; padding: 14px; margin: 12px 0; }
    label { display: inline-flex; align-items: center; gap: 6px; margin: 4px 10px 4px 0; font-size: 14px; }
    input, select { height: 32px; border: 1px solid #bbb; border-radius: 6px; padding: 0 8px; font-size: 14px; }
    input[type="number"] { width: 76px; }
    button { height: 36px; border: 1px solid #222; border-radius: 7px; background: #222; color: white; padding: 0 12px; font-weight: 600; cursor: pointer; }
    button.secondary { background: #fff; color: #222; }
    button.danger { background: #b00020; border-color: #b00020; }
    .grid { display: grid; grid-template-columns: repeat(3, 70px); gap: 8px; width: max-content; }
    .row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    pre { background: #101418; color: #d7ffd7; min-height: 180px; max-height: 360px; overflow: auto; padding: 12px; border-radius: 8px; white-space: pre-wrap; }
    .muted { color: #666; font-size: 13px; }
  </style>
</head>
<body>
<main>
  <h1>OpenBot Serial Control</h1>

  <section>
    <div class="row">
      <select id="port"></select>
      <button onclick="refreshPorts()" class="secondary">Refresh Ports</button>
      <button onclick="connect()">Connect</button>
      <button onclick="send('PING')">Ping</button>
      <button onclick="send('STATUS')">Status</button>
      <button onclick="send('CAL?')">Cal?</button>
      <button onclick="send('IMU?', 0.5)">IMU?</button>
      <button onclick="diagnostics()" class="secondary">Diagnostics</button>
      <button onclick="send('STOP')" class="danger">Stop</button>
    </div>
    <div class="muted">Commands are sent as newline-terminated text over USB serial.</div>
  </section>

  <section>
    <h2>Drive</h2>
    <div class="row">
      <label>Power <input id="power" type="number" value="35" min="0" max="100"></label>
      <label>Turn <input id="turn" type="number" value="25" min="0" max="100"></label>
      <label>Duration ms <input id="ms" type="number" value="300" min="1" max="1500"></label>
    </div>
    <div class="grid">
      <span></span><button onclick="drive(0, val('power'), 0)">W</button><span></span>
      <button onclick="drive(-val('power'), 0, 0)">A</button><button class="danger" onclick="send('STOP')">X</button><button onclick="drive(val('power'), 0, 0)">D</button>
      <button onclick="drive(0, 0, -val('turn'))">Q</button><button onclick="drive(0, -val('power'), 0)">S</button><button onclick="drive(0, 0, val('turn'))">E</button>
    </div>
  </section>

  <section>
    <h2>Raw Wheels</h2>
    <div class="row">
      <label>FL <input id="fl" type="number" value="0" min="-100" max="100"></label>
      <label>FR <input id="fr" type="number" value="0" min="-100" max="100"></label>
      <label>BL <input id="bl" type="number" value="0" min="-100" max="100"></label>
      <label>BR <input id="br" type="number" value="0" min="-100" max="100"></label>
      <button onclick="wheel()">Send Wheel</button>
    </div>
  </section>

  <section>
    <h2>Calibration</h2>
    <div class="row">
      <label>Motor
        <select id="motor"><option value="1">1 FL</option><option value="2">2 FR</option><option value="3">3 BL</option><option value="4">4 BR</option></select>
      </label>
      <label>Magnitude <input id="mag" type="number" value="30" min="0" max="400"></label>
      <button onclick="setCal('SETF')">Set Forward</button>
      <button onclick="setCal('SETB')">Set Backward</button>
    </div>
  </section>

  <section>
    <h2>Raw Command</h2>
    <div class="row">
      <input id="raw" style="width: min(600px, 100%)" value="DRIVE 0 35 0 300">
      <button onclick="send(document.getElementById('raw').value, 0.6)">Send</button>
    </div>
  </section>

  <section>
    <h2>Log</h2>
    <pre id="log"></pre>
  </section>
</main>

<script>
function val(id) { return Number(document.getElementById(id).value); }
function log(msg) {
  const el = document.getElementById('log');
  el.textContent += msg + "\n";
  el.scrollTop = el.scrollHeight;
}
async function api(path, body) {
  const res = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body || {})});
  const data = await res.json();
  if (!res.ok) throw new Error(data.detail || JSON.stringify(data));
  return data;
}
async function refreshPorts() {
  const res = await fetch('/api/ports');
  const data = await res.json();
  const select = document.getElementById('port');
  select.innerHTML = '';
  for (const p of data.ports) {
    const opt = document.createElement('option');
    opt.value = p; opt.textContent = p;
    select.appendChild(opt);
  }
  log('ports: ' + data.ports.join(', '));
  if (data.connected) log('connected: ' + data.connected);
}
async function connect() {
  try {
    const port = document.getElementById('port').value || null;
    const data = await api('/api/connect', {port});
    log('connected: ' + JSON.stringify(data));
  } catch (e) { log('ERR connect: ' + e.message); }
}
async function diagnostics() {
  try {
    const res = await fetch('/api/diagnostics');
    const data = await res.json();
    log('diagnostics: ' + JSON.stringify(data, null, 2));
  } catch (e) { log('ERR diagnostics: ' + e.message); }
}
async function send(command, wait_s=0.35) {
  try {
    log('> ' + command);
    const data = await api('/api/command', {command, wait_s});
    for (const line of data.lines) log('< ' + line);
  } catch (e) { log('ERR command: ' + e.message); }
}
function drive(x, y, turn) {
  send(`DRIVE ${x} ${y} ${turn} ${val('ms')}`, 0.6);
}
function wheel() {
  send(`WHEEL ${val('fl')} ${val('fr')} ${val('bl')} ${val('br')} ${val('ms')}`, 0.6);
}
function setCal(which) {
  send(`${which} ${document.getElementById('motor').value} ${val('mag')}`);
}
refreshPorts();
</script>
</body>
</html>
"""


@app.get("/", response_class=HTMLResponse)
def index() -> str:
    return HTML


@app.get("/api/ports")
def ports() -> dict:
    return {"ports": bridge.list_ports(), "connected": bridge.port}


@app.get("/api/diagnostics")
def diagnostics() -> dict:
    return bridge.diagnostics()


@app.post("/api/connect")
def connect(req: ConnectRequest) -> JSONResponse:
    try:
      return JSONResponse({"ok": True, **bridge.connect(req.port)})
    except Exception as exc:
      return JSONResponse({"ok": False, "detail": str(exc)}, status_code=400)


@app.post("/api/command")
def command(req: CommandRequest) -> JSONResponse:
    try:
      return JSONResponse({"ok": True, **bridge.command(req.command, req.wait_s)})
    except Exception as exc:
      return JSONResponse({"ok": False, "detail": str(exc)}, status_code=400)
