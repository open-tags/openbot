# OpenBot Serial Bridge

Text protocol for a Jetson connected to a Seeed XIAO ESP32-S3 over USB serial.

All commands are newline terminated. Responses begin with `OK` or `ERR`.

## Firmware From The Jetson

Plug the XIAO into the Jetson over USB. The stable XIAO port usually appears as:

```bash
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_CC:BA:97:0F:79:20-if00
```

One-time setup:

```bash
cd ~/openbot/software/jetson
bash setup.sh
```

If serial access says `Permission denied`, add the Jetson user to `dialout` and restart the login session:

```bash
sudo usermod -aG dialout $USER
sudo reboot
```

Build and upload this firmware from the repo:

```bash
cd ~/openbot
pio run -d firmware/control
pio run -d firmware/control -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_CC:BA:97:0F:79:20-if00
```

Open a direct serial monitor:

```bash
pio device monitor -d firmware/control --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_CC:BA:97:0F:79:20-if00 --baud 115200
```

## Jetson Web GUI

The FastAPI control GUI lives in `software/jetson/gui/`. Copy it to the Jetson and start it:

```bash
scp -r software/jetson/gui/* jetson:~/openbot_gui/
ssh jetson 'cd ~/openbot_gui && python3 -m pip install --user --break-system-packages -r requirements.txt'
ssh jetson 'cd ~/openbot_gui && bash start_gui.sh'
```

Then open:

```text
http://100.119.207.112:8000/
```

Use **Diagnostics** if Connect fails. The most common cause is the Jetson user not being in `dialout`.

## Tested Jetson State

Current Jetson address:

```text
100.119.207.112
```

The web GUI is reachable at:

```text
http://100.119.207.112:8000/
```

Verified:

```text
FastAPI GUI starts and serves /api/diagnostics.
PlatformIO is installed under the Jetson user Python.
Firmware builds successfully on the Jetson from `~/openbot/firmware/control`.
```

Blocked until the `dialout` fix is applied:

```text
GUI Connect to XIAO serial.
PlatformIO upload to XIAO serial.
```

The diagnostic symptom is:

```text
owner=root group=dialout mode=0o660 can_read=false can_write=false
```

After `sudo usermod -aG dialout $USER && sudo reboot`, diagnostics should show `dialout: true` and `can_read/can_write: true` for the XIAO port.

## Commands

```text
PING
HELP
STOP
STATUS
CAL?
IMU?
YAW?
SETF <motor 1..4> <us>
SETB <motor 1..4> <us>
WHEEL <fl -100..100> <fr -100..100> <bl -100..100> <br -100..100> <ms>
DRIVE <x -100..100> <y -100..100> <turn -100..100> <ms>
```

Motor IDs:

```text
1 = FL / D1
2 = FR / D3
3 = BL / D2
4 = BR / D0
```

`IMU?` and `YAW?` only run while motors are stopped. This avoids I2C reads stretching software ESC pulses.
