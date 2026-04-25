# Joe's Calibrage

Calibrate and persist thumbstick values for NextUI on Miyoo Flip, TrimUI Smart Pro, and TrimUI Smart Pro S devices. This Pak uses Apostrophe for a native UI and writes the calibration files consumed by each device's input daemon.

## Supported Platforms

| Platform | Device | Screen | Build |
|----------|--------|--------|-------|
| `my355` | Miyoo Flip | 640x480 | Docker (ARM64) |
| `tg5040` | TrimUI Smart Pro | 1280x720 | Docker (ARM64) |
| `tg5050` | TrimUI Smart Pro S | 1280x720 | Docker (ARM64) |

## What It Does

- Shows live thumbstick movement for testing
- Calibrates the left and right sticks separately
- Saves calibration so it survives reboot
- Creates first-run backups before replacing existing calibration files
- Restores those backups from the UI
- Keeps calibration data isolated per platform when the same SD card moves between devices

## Usage

1. Launch **Joe's Calibrage** from the NextUI Tools menu.
2. Open **Test Sticks** to confirm the live dots move correctly.
3. Choose **Calibrate Left** or **Calibrate Right**.
4. Rotate the stick fully around the edge until the screen says **Range captured. Press A.**
5. Press **A Next**.
6. Release the stick and keep it centered.
7. Press **Y Save**.
8. Return to **Test Sticks** to confirm the calibrated movement.

## Menu Options

### Test Sticks

Shows both sticks as circles with live dots from the normal SDL input device. Use this screen to verify the calibration that games and NextUI will see.

Footer:

| Button | Action |
|--------|--------|
| **B Back** | Return to the menu |

### Calibrate Left / Calibrate Right

Runs a two-step calibration flow for one stick:

1. Rotate fully around the outside edge and press **A Next**.
2. Let the stick rest in the center and press **Y Save**.

Footer:

| Button | Action |
|--------|--------|
| **B Cancel** | Leave calibration without saving |
| **X Reset** | Clear the current samples and start again |
| **A Next** | Move from range capture to center capture |
| **Y Save** | Save calibration during center capture |

### View Values

Shows the current saved calibration values, platform diagnostics, runtime config path, SD mirror path, and raw input source.

Footer:

| Button | Action |
|--------|--------|
| **A OK** | Return to the menu |

### Restore Backup

Restores the first-run backup files for both sticks. Backups are only available after the tool has seen existing calibration files and saved over them at least once.

## Saved Files

Each platform writes the six calibration fields used by the stock input daemon:

```text
x_min
x_max
y_min
y_max
x_zero
y_zero
```

There are two stick files on every platform:

| File | Stick |
|------|-------|
| `joypad.config` | Left stick |
| `joypad_right.config` | Right stick |

### File Roles

| Role | Meaning |
|------|---------|
| Active runtime config | The file the device input daemon reads and applies. |
| SD source / mirror | A platform-isolated copy on the SD card. On `my355` this is the persistent source. On TrimUI devices this is a mirror for portability, backup, and inspection. |
| Backup | A first-run backup of a file before Joe's Calibrage replaces it. Backups sit next to the original file with `.bak` appended. Existing backups are not overwritten. |
| Reload / restart signal | A temporary file used to tell the input daemon that calibration changed. |
| Calibration grab flag | A temporary file used while raw calibration is running so the stock input daemon backs off from the raw stick stream. |

### my355 / Miyoo Flip

Active runtime config:

```text
/userdata/joypad.config
/userdata/joypad_right.config
```

SD persistent source:

```text
/mnt/SDCARD/.userdata/my355/userdata/joypad.config
/mnt/SDCARD/.userdata/my355/userdata/joypad_right.config
```

Backups, when originals existed before first save:

```text
/mnt/SDCARD/.userdata/my355/userdata/joypad.config.bak
/mnt/SDCARD/.userdata/my355/userdata/joypad_right.config.bak
/userdata/joypad.config.bak
/userdata/joypad_right.config.bak
```

On NextUI, `/userdata` may be backed by the same SD files. If both paths resolve to the same file, Joe's Calibrage writes it once and uses the same backup.

Temporary files:

```text
/tmp/joypad_calibrating
/tmp/miyoo_inputd/cal_update
```

`/tmp/miyoo_inputd/cal_update` tells `miyoo_inputd` to reload `/userdata/joypad.config` and `/userdata/joypad_right.config`.

### tg5040 / TrimUI Smart Pro

Active runtime config used by `trimui_inputd`:

```text
/mnt/UDISK/joypad.config
/mnt/UDISK/joypad_right.config
```

SD mirror:

```text
/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad.config
/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad_right.config
```

Backups, when originals existed before first save:

```text
/mnt/UDISK/joypad.config.bak
/mnt/UDISK/joypad_right.config.bak
/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad.config.bak
/mnt/SDCARD/.userdata/tg5040/joes-calibrage/joypad_right.config.bak
```

Temporary files:

```text
/tmp/trimui_inputd/grab
/tmp/trimui_inputd_restart
```

`/mnt/UDISK/joypad*.config` is the source that `trimui_inputd` actually consumes. The SD mirror is not read by stock `trimui_inputd`; it exists so the values are visible on the SD card and stay isolated from other platforms. `/tmp/trimui_inputd_restart` is used by the Pak to restart `trimui_inputd` after saving.

### tg5050 / TrimUI Smart Pro S

Active runtime config used by `trimui_inputd`:

```text
/mnt/UDISK/joypad.config
/mnt/UDISK/joypad_right.config
```

SD mirror:

```text
/mnt/SDCARD/.userdata/tg5050/joes-calibrage/joypad.config
/mnt/SDCARD/.userdata/tg5050/joes-calibrage/joypad_right.config
```

Backups, when originals existed before first save:

```text
/mnt/UDISK/joypad.config.bak
/mnt/UDISK/joypad_right.config.bak
/mnt/SDCARD/.userdata/tg5050/joes-calibrage/joypad.config.bak
/mnt/SDCARD/.userdata/tg5050/joes-calibrage/joypad_right.config.bak
```

Temporary files:

```text
/tmp/trimui_inputd/grab
/tmp/trimui_inputd/cal_update
```

`/mnt/UDISK/joypad*.config` is the source that `trimui_inputd` actually consumes. The SD mirror is not read by stock `trimui_inputd`; it exists so the values are visible on the SD card and stay isolated from other platforms. `/tmp/trimui_inputd/cal_update` tells `trimui_inputd` to reload without restarting.

### Save and Restore Behavior

When saving calibration, Joe's Calibrage:

1. Reads the existing active file if present.
2. Creates a `.bak` backup next to it if that backup does not already exist.
3. Writes a temporary file named like `joypad.config.tmp.<pid>`.
4. Flushes it, renames it over the target file, and calls `sync`.
5. Mirrors the saved values to the platform SD path when applicable.
6. Triggers the platform reload or restart signal.

**Restore Backup** reads the `.bak` files, writes them back to the active paths, mirrors the restored values, and triggers the same reload or restart path. If no `.bak` files exist yet, restore reports that no backups were found.

## Notes

- Calibration affects the normal input daemon, not only this app.
- `tg5040` may need an input daemon restart after saving; the Pak handles this automatically.
- `tg5050` uses the live `/tmp/trimui_inputd/cal_update` reload hook.
- The calibration preview dot is clamped to the on-screen circle. Saved calibration values remain raw per-axis min/max/zero values because that is the format consumed by the input daemon.

## Logging

Logs are written to:

```text
/mnt/SDCARD/.userdata/shared/logs/joes-calibrage.txt
```

## Installing on a Handheld

1. Download `Joe's Calibrage.pakz`.
2. Extract it to the root of your SD card.
3. Confirm the result contains one or more of:

```text
Tools/my355/Joe's Calibrage.pak/
Tools/tg5040/Joe's Calibrage.pak/
Tools/tg5050/Joe's Calibrage.pak/
```

4. Launch **Joe's Calibrage** from NextUI Tools.

## Building

### Prerequisites

**macOS (development):**

```bash
brew install sdl2 sdl2_ttf sdl2_image
```

**Embedded (my355/tg5040/tg5050):**

- Docker with ARM64 support

### First-Time Setup

```bash
git submodule update --init
```

### Build Commands

```bash
# Run native unit tests
make test-native

# Build for macOS development
make mac

# Build for a specific platform
make my355
make tg5040
make tg5050

# Package per-platform .pak.zip files and a combined .pakz
make package

# Detect an adb target and deploy the matching build
make deploy

# See all targets
make help
```

### Output

| Target | Output |
|--------|--------|
| my355 | `build/release/my355/Joe's Calibrage.pak.zip` |
| tg5040 | `build/release/tg5040/Joe's Calibrage.pak.zip` |
| tg5050 | `build/release/tg5050/Joe's Calibrage.pak.zip` |
| package | `build/release/all/Joe's Calibrage.pakz` |
