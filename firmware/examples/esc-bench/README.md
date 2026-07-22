# demo bot firmware — XIAO ESP32-S3

Bench-safe tester for **4× BLHeli_S ESC** (analog/servo PWM) + an **I2C IMU**.

## Flash & test (USB-C, from your terminal)

```bash
cd openbot/firmware
./deploy.sh            # build + flash + open serial monitor
```

Other modes:

```bash
./deploy.sh build      # compile only — no board needed (good first sanity check)
./deploy.sh upload     # build + flash, no monitor
./deploy.sh monitor    # serial monitor only
./deploy.sh ports      # show detected USB serial ports
```

First run installs PlatformIO automatically. If the **first** flash fails, put the
board in bootloader mode: hold **BOOT**, tap **RESET**, release **BOOT**, re-run.

## Wiring (see XIAO ESP32-S3 pinout)

| Signal | XIAO pin | GPIO |
|--------|----------|------|
| ESC1   | D0       | 1    |
| ESC2   | D1       | 2    |
| ESC3   | D2       | 3    |
| ESC4   | D3       | 4    |
| IMU SDA| D4       | 5    |
| IMU SCL| D5       | 6    |

- **Common ground** between XIAO, all 4 ESCs, and the IMU is mandatory.
- ESCs have **no BEC** → power the XIAO from USB-C or a separate 5 V regulator.
- Only the ESC **signal + ground** wires go to the XIAO.

## Serial commands (115200 baud)

| Key      | Action |
|----------|--------|
| `a`      | Arm all ESCs (min-throttle hold) |
| `0`–`9`  | Throttle 0%,10%,…,90% on the selected motor(s) |
| `+` / `-`| Nudge selected throttle ±5% |
| `n`      | Cycle selection: ALL → M1 → M2 → M3 → M4 → ALL |
| `s`/space| **EMERGENCY STOP** all motors |
| `i`      | I2C bus scan (find the IMU address) |
| `?`      | Help + status |

`SAFE_MAX_PCT` in `src/main.cpp` caps throttle at 40% for bench testing — raise it
once you trust the setup.

## ⚠️ Safety
**PROPS OFF for all testing.** Strap motors down before the first spin.
