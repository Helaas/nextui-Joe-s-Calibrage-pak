# my355 Stick Calibration Research

## Goal

Joe's Calibrage is a my355-only v1 tool for calibrating the Miyoo Flip thumbsticks from NextUI and persisting the values across reboots.

Later work can extend the same app to `tg5040` and `tg5050`, but those platforms are intentionally out of scope for v1.

## Live my355 Findings

- Device tree model: `MIYOO RK3566 355 V10 Board`.
- Kernel: RK3566 Buildroot Linux 5.10.160.
- SD card mount observed on stock OS: `/mnt/sdcard`; NextUI scripts use `/mnt/SDCARD`.
- NextUI bind-mount source for stock userdata: `/mnt/SDCARD/.userdata/my355/userdata`.
- Runtime userdata path after bind mount: `/userdata`.
- Input device exposed to Linux/SDL: `MIYOO Player1` at `/dev/input/event5` and `/dev/input/js0`.
- Stock boot sets `/sys/class/miyooio_chr_dev/joy_type` to `-1`; the node text is formatted like `-1 [-1=none 0=miyoo 1=xbox]`, so callers must parse only the first token.

## Calibration Files

Stock reads these plain-text files:

```text
/userdata/joypad.config
/userdata/joypad_right.config
```

The live NextUI stock-SD setup mirrors them here:

```text
/mnt/SDCARD/.userdata/my355/userdata/joypad.config
/mnt/SDCARD/.userdata/my355/userdata/joypad_right.config
```

Each file contains:

```text
x_min=20
x_max=216
y_min=35
y_max=216
x_zero=116
y_zero=130
```

The stock daemon logs these as `[x_min x_zero x_max] [y_min y_zero y_max]` via `pk_read_cal_config`.

## Raw Stick Source

Initial suspicion was `/dev/miyooio`, because both `MainUI` and `miyoo_inputd` reference it. Disassembly and live reads showed the calibration-grade raw stick stream is actually on `/dev/ttyS1`.

Stock `miyoo_inputd` opens `/dev/ttyS1` at 9600 baud, 8N1, and reads a six-byte packet:

```text
ff ly lx ry rx fe
```

Live idle sample:

```text
ff 82 74 7d 7b fe
```

Mapping:

- `lx`: left stick X raw, 0-255.
- `ly`: left stick Y raw, 0-255.
- `rx`: right stick X raw, 0-255.
- `ry`: right stick Y raw, 0-255.

This matches the scale and field order of `joypad.config` and `joypad_right.config`. SDL axes are already normalized through the current calibration and must not be used to generate min/max/zero values.

The test screen should use SDL joystick axes only. Raw serial reads are reserved for the calibration flow so the app does not compete with `miyoo_inputd` while the user is only checking stick behavior.

## Runtime Update Contract

After writing calibration files, stock `MainUI` touches:

```text
/tmp/miyoo_inputd/cal_update
```

`miyoo_inputd` watches that path and reloads `/userdata/joypad.config` and `/userdata/joypad_right.config`.

During stock calibration, `MainUI` also uses:

```text
/tmp/joypad_calibrating
```

The v1 app creates this flag before opening `/dev/ttyS1`, samples raw values only while the flag exists, and removes it on exit.

## Persistence Strategy

The tool writes both the SD-backed source and the runtime mirror:

```text
/mnt/SDCARD/.userdata/my355/userdata/joypad*.config
/userdata/joypad*.config
```

If those paths resolve to the same bind-mounted file, the runtime write is skipped. The first save creates `*.bak` files and never replaces an existing backup.

Write sequence:

1. Parse and validate the new config.
2. Create parent directories if needed.
3. Create first-run backup if the target exists and no backup exists.
4. Write a temp file in the same directory.
5. `fsync`, close, and `rename`.
6. `sync`.
7. Touch `/tmp/miyoo_inputd/cal_update`.

## External References for Future TrimUI Work

KNULLI's TrimUI Smart Pro documentation and release notes confirm that TrimUI Smart Pro calibration also revolves around `joypad.config` and `joypad_right.config`, but their persistence location and input daemon need separate live-device verification:

- https://knulli.org/pl/devices/trimui/smart-pro/
- https://github.com/knulli-cfw/distribution/releases

## Open Follow-Ups

- Validate full left/right calibration on-device from inside NextUI, including reboot persistence.
- Confirm in a full handheld session that `miyoo_inputd` stops consuming `/dev/ttyS1` while `/tmp/joypad_calibrating` exists; the app avoids raw serial reads outside calibration.
- Research tg5040 and tg5050 only after my355 v1 is proven.
