# Thumbstick Calibration Research

## Goal

Joe's Calibrage calibrates handheld thumbsticks from NextUI and persists the values across reboots.

Supported platforms:

- `my355` / Miyoo Flip.
- `tg5040` / TrimUI Smart Pro.

Future work:

- `tg5050` / TrimUI Smart Pro S still needs the same stock-binary and live-device investigation before support is added.

## Shared Config Format

Both verified platforms use the same six plain-text fields:

```text
x_min=20
x_max=216
y_min=35
y_max=216
x_zero=116
y_zero=130
```

The stock daemons log or store these values as `[x_min x_zero x_max] [y_min y_zero y_max]`, but the file order remains `min`, `max`, `zero`.

SDL axes are already normalized through the active calibration. The app uses SDL only for the Test Sticks screen; calibration uses raw device streams.

## my355 / Miyoo Flip

Live findings:

- Device tree model: `MIYOO RK3566 355 V10 Board`.
- Kernel: RK3566 Buildroot Linux 5.10.160.
- SD card mount observed on stock OS: `/mnt/sdcard`; NextUI scripts use `/mnt/SDCARD`.
- NextUI bind-mount source for stock userdata: `/mnt/SDCARD/.userdata/my355/userdata`.
- Runtime userdata path after bind mount: `/userdata`.
- Input device exposed to Linux/SDL: `MIYOO Player1` at `/dev/input/event5` and `/dev/input/js0`.
- Stock boot sets `/sys/class/miyooio_chr_dev/joy_type` to `-1`; the node text is formatted like `-1 [-1=none 0=miyoo 1=xbox]`, so callers must parse only the first token.

Calibration files:

```text
/mnt/SDCARD/.userdata/my355/userdata/joypad.config
/mnt/SDCARD/.userdata/my355/userdata/joypad_right.config
/userdata/joypad.config
/userdata/joypad_right.config
```

Raw stick source:

- `/dev/ttyS1`.
- 9600 baud, 8N1.
- Six-byte packet: `ff ly lx ry rx fe`.
- Raw scale: 0-255.

Runtime update contract:

- Create `/tmp/joypad_calibrating` while sampling raw serial data.
- After writing calibration files, touch `/tmp/miyoo_inputd/cal_update`.
- `miyoo_inputd` reloads `/userdata/joypad.config` and `/userdata/joypad_right.config`.

Persistence strategy:

- Write the SD-backed source first.
- Write `/userdata` unless it is the same bind-mounted file.
- Create `*.bak` first-run backups and never replace existing backups.

## tg5040 / TrimUI Smart Pro

Live findings from the connected stock OS unit:

- Device tree model: `sun50iw10`.
- SoC family: Allwinner A133.
- Kernel: TinaLinux 4.9.191 aarch64.
- SD card path: `/mnt/SDCARD`.
- Internal stock persistence path: `/mnt/UDISK`.
- Linux/SDL input device: `TRIMUI Player1` at `/dev/input/event3` and `/dev/input/js0`.
- Stock input daemon: `trimui_inputd`.
- NextUI launches `trimui_inputd` from `SYSTEM/tg5040/paks/MinUI.pak/launch.sh`.

Stock `MainUI` and `trimui_inputd` strings/disassembly show these calibration files:

```text
/mnt/UDISK/joypad.config
/mnt/UDISK/joypad_right.config
```

If files are missing, `trimui_inputd` uses these defaults:

```text
x_min=1050
x_max=2900
y_min=1050
y_max=2900
x_zero=2150
y_zero=2150
```

Raw stick sources:

- Left stick: `/dev/ttyS4`.
- Right stick: `/dev/ttyS3`.
- 19200 baud, 8N1.
- Eight-byte packet: `ff ?? buttons x_hi x_lo y_hi y_lo fe`.
- X/Y are big-endian unsigned 16-bit values.

Runtime update contract:

- `trimui_inputd` reads calibration at startup.
- No my355-style `cal_update` watcher was found.
- Stock `MainUI` signals restart by touching `/tmp/trimui_inputd_restart`.
- During raw calibration, create `/tmp/trimui_inputd/grab` so `trimui_inputd` stops emitting normal input. Do not create `/tmp/keytestmode`; if both exist, stock code continues processing packets.
- Live probing showed `grab` and `/tmp/system_suspend` do not give the tool exclusive serial access. `trimui_inputd` keeps `/dev/ttyS3` and `/dev/ttyS4` open and continues draining packets.
- Reliable calibration requires pausing `trimui_inputd`, opening both raw tty streams, using the selected stick's X/Y samples, and reading A/B/X/Y directly from the right-stick packet button byte.
- Raw face-button bits observed while `trimui_inputd` was paused: `A=0x10`, `B=0x20`, `X=0x04`, `Y=0x08`.

Persistence strategy:

- Runtime source of truth is `/mnt/UDISK/joypad*.config` because stock `trimui_inputd` hardcodes that path.
- Mirror successful saves to `/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad*.config`.
- The SD mirror is platform-isolated and is not read by my355.
- On app exit, `launch.sh` removes `/tmp/trimui_inputd/grab` and restarts `trimui_inputd` if `/tmp/trimui_inputd_restart` exists.

## Write Safety

For every target file:

1. Parse and validate the new config.
2. Create parent directories if needed.
3. Create a first-run backup if the target exists and no backup exists.
4. Write a temp file in the same directory.
5. `fsync`, close, and `rename`.
6. `sync`.
7. Trigger the platform reload or restart signal.

## External References for Future TrimUI Work

KNULLI's TrimUI Smart Pro documentation and release notes confirm that TrimUI Smart Pro calibration also revolves around `joypad.config` and `joypad_right.config`, but each platform still needs live verification before support is shipped:

- https://knulli.org/pl/devices/trimui/smart-pro/
- https://github.com/knulli-cfw/distribution/releases
