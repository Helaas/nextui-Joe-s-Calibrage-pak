# Thumbstick Calibration Research

## Goal

Joe's Calibrage calibrates handheld thumbsticks from NextUI and persists the values across reboots.

Supported platforms:

- `my355` / Miyoo Flip.
- `tg5040` / TrimUI Smart Pro.
- `tg5050` / TrimUI Smart Pro S.

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
- After saving or restoring calibration, the app restarts `trimui_inputd` immediately, waits briefly for the virtual joystick to return, and calls the locally patched Apostrophe `ap_refresh_input()` hook so Test Sticks reflects the new normalized values without closing Joe's Calibrage.

Persistence strategy:

- Runtime source of truth is `/mnt/UDISK/joypad*.config` because stock `trimui_inputd` hardcodes that path.
- Mirror successful saves to `/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad*.config`.
- The SD mirror is platform-isolated and is not read by my355.
- On app exit, `launch.sh` still removes `/tmp/trimui_inputd/grab` and restarts `trimui_inputd` if `/tmp/trimui_inputd_restart` exists. This is a fallback for abnormal exits; normal save/restore applies the restart inside the app.

## tg5050 / TrimUI Smart Pro S

Live findings from the connected NextUI unit:

- Device tree model: `sun55iw3`.
- SoC family: Allwinner A523.
- Kernel: Linux 5.15.147 aarch64.
- SD card path: `/mnt/SDCARD`, a symlink to `/mnt/sdcard/mmcblk1p1`.
- Internal runtime persistence path: `/mnt/UDISK`.
- Linux/SDL input device: `TRIMUI Player1` at `/dev/input/event4` and `/dev/input/js0`.
- Runtime input daemon: `/usr/trimui/bin/trimui_inputd`.

`trimui_inputd` strings/disassembly show these calibration files:

```text
/mnt/UDISK/joypad.config
/mnt/UDISK/joypad_right.config
```

If files are missing, `trimui_inputd` uses these defaults:

```text
x_min=560
x_max=3600
y_min=400
y_max=3600
x_zero=2048
y_zero=2048
```

Raw stick sources:

- Left stick: `/dev/ttyAS5`.
- Right stick: `/dev/ttyAS7`.
- 19200 baud, 8N1.
- Twenty-byte packet with `0xff` at byte `0` and `0xfe` at byte `18`.
- Button mask is little-endian u32 in bytes `2..5`.
- Left X/Y are little-endian u16 at bytes `6` and `8`.
- Right X/Y are little-endian u16 at bytes `10` and `12`.
- Raw face-button bits observed from daemon disassembly depend on `/var/trimui_inputd/rotate_270`.
  A live no-rotate capture on 2026-04-26 mapped `A=0x00000100`, `B=0x00000001`, `X=0x00000200`, `Y=0x00000002`.
  With rotate flag present, it maps `A=0x00000001`, `B=0x00000002`, `X=0x00010000`, `Y=0x00020000`.
- Raw Y increases opposite to the app-facing SDL axis, so the calibration preview dot flips Y for display only; saved calibration values stay in the raw daemon coordinate system.
- The persisted format is per-axis min/max/zero only. Diagonal raw positions can normalize to a vector longer than one because both axes are near their limits at once, so the UI clamps the preview dot to the circular guide for display only.

Runtime update contract:

- During raw calibration, create `/tmp/trimui_inputd/grab` and pause `trimui_inputd`; live probing and file descriptors showed the daemon keeps `/dev/ttyAS5` and `/dev/ttyAS7` open.
- After writing calibration files, touch `/tmp/trimui_inputd/cal_update`.
- The daemon has a live `cal_update` hook, so tg5050 does not need the tg5040 daemon restart path for normal saves.

Persistence strategy:

- Runtime source of truth is `/mnt/UDISK/joypad*.config`.
- Mirror successful saves to `/mnt/SDCARD/.userdata/tg5050/joes-calibrage/joypad*.config`.
- The SD mirror is platform-isolated and is not read by my355 or tg5040.
- On app exit, `launch.sh` removes `/tmp/trimui_inputd/grab` and resumes `trimui_inputd` if calibration exited abnormally.

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
