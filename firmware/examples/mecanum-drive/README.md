# Simple mecanum bot firmware

XIAO ESP32-S3 firmware for the four-motor mecanum demo bot.

## Flash

From `firmware/`:

```bash
./deploy.sh build
./deploy.sh upload
./deploy.sh monitor
```

`./deploy.sh` defaults to this mecanum project. The older ESC tester is still available with `./deploy.sh esc build`.

## Motor Map

| Motor | Pin | Default center | Default direction |
|---|---:|---:|---:|
| `0` front left | D2 | 1500 us | -1 |
| `1` front right | D3 | 1500 us | +1 |
| `2` back left | D1 | 1540 us | -1 |
| `3` back right | D0 | 1540 us | +1 |

## Serial Commands

Use 115200 baud. Type a command and press Enter.

| Command | Action |
|---|---|
| `w` / `s` | Forward / back |
| `a` / `d` | Strafe left / right |
| `q` / `e` or `-` / `+` | Rotate counter-clockwise / clockwise |
| `drive x y t` | Custom mix, each axis `-100..100` |
| `x` or space at empty prompt | Stop and center |
| `c m us` | Set motor center, for example `c 1 1495` |
| `trim m delta` | Add to a motor center, for example `trim 0 -20` |
| `inv m` | Flip one motor direction |
| `gain m pct` | Scale one motor output, `20..200` |
| `power us` | Set drive strength around center; start with `30..60` |
| `time ms` | Set keyboard move duration |
| `test m` / `u` | Test one motor / all motors; `x` or space aborts |
| `raw m us` | Direct-write a motor pulse |
| `off m` | Detach one motor signal and hold pin LOW |
| `p` / `?` | Print tuning / help |
| `save` / `load` / `defaults` | Persist, reload, or restore tuning |

First tune with wheels off the floor. Once forward/back/strafe/rotate all point the right way, run `save`.
