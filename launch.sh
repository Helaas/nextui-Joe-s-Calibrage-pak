#!/bin/sh
set -eu

APP_BIN="joes-calibrage"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAK_NAME=$(basename "$PAK_DIR")
PAK_NAME=${PAK_NAME%.pak}

cd "$PAK_DIR"

if [ -n "${SHARED_USERDATA_PATH:-}" ]; then
    SHARED_USERDATA_ROOT="$SHARED_USERDATA_PATH"
elif [ -d "/mnt/SDCARD/.userdata/shared" ] || [ -d "/mnt/SDCARD" ]; then
    SHARED_USERDATA_ROOT="/mnt/SDCARD/.userdata/shared"
else
    SHARED_USERDATA_ROOT="${HOME:-/tmp}/.userdata/shared"
fi

LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$APP_BIN.txt"
: >"$LOG_FILE"

exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching $PAK_NAME ($APP_BIN) at $(date) ==="
echo "platform=${PLATFORM:-unknown} device=${DEVICE:-unknown}"
echo "args: $*"

"./$APP_BIN" "$@"
STATUS=$?

case "${PLATFORM:-}" in
tg5040|tg5050)
    rm -f /tmp/trimui_inputd/grab
    for pid in $(pidof trimui_inputd 2>/dev/null || true); do
        kill -CONT "$pid" 2>/dev/null || true
    done
    ;;
esac

if [ "${PLATFORM:-}" = "tg5040" ]; then
    if [ -f /tmp/trimui_inputd_restart ]; then
        echo "Restarting trimui_inputd for updated calibration."
        killall -9 trimui_inputd >/dev/null 2>&1 || true
        sleep 0.2
        if command -v trimui_inputd >/dev/null 2>&1; then
            trimui_inputd >/dev/null 2>&1 &
        fi
        rm -f /tmp/trimui_inputd_restart
    fi
fi

exit "$STATUS"
