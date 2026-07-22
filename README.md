# OpenBot

Open Tags mobile robot prototype.

![OpenBot assembly](cad/openbot-top.png)

- [CAD (Onshape)](https://cad.onshape.com/documents/432dec42a257744f3b8f22d9/w/829cba9d7a4b997211a6277e/e/69cb96d6d8647e33e3457e0e)
- [STEP assembly](cad/openbot_assembly.step)
- `firmware/control/` — main XIAO ESP32-S3 firmware
- `firmware/examples/` — motor and IMU bring-up projects
- `software/jetson/` — Jetson control UI
- `viewer/` — browser-based 3D assembly viewer

Run the viewer from the repository root:

```bash
python3 -m http.server 8000
```

Then open <http://localhost:8000/viewer/>.
