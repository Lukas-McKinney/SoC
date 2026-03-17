#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BINARY_PATH="${BINARY_PATH:-$ROOT_DIR/settlers}"
HOST_IP="${1:-127.0.0.1}"
PORT="${PORT:-24680}"
HOST_PLAYER="${HOST_PLAYER:-red}"
JOIN_PLAYER="${JOIN_PLAYER:-blue}"
AI_DIFFICULTY="${AI_DIFFICULTY:-medium}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This helper is for macOS only." >&2
  exit 1
fi

if [[ ! -x "$BINARY_PATH" ]]; then
  echo "Binary not found or not executable: $BINARY_PATH" >&2
  echo "Build first with: make" >&2
  exit 1
fi

host_cmd="cd \"$ROOT_DIR\"; \"$BINARY_PATH\" --host --player $HOST_PLAYER --remote-player $JOIN_PLAYER --port $PORT --ai-difficulty $AI_DIFFICULTY"
join_cmd="cd \"$ROOT_DIR\"; sleep 1; \"$BINARY_PATH\" --join \"$HOST_IP\" --player $JOIN_PLAYER --remote-player $HOST_PLAYER --port $PORT"

osascript - "$host_cmd" "$join_cmd" <<'APPLESCRIPT'
on run argv
  tell application "Terminal"
    activate
    do script (item 1 of argv)
    do script (item 2 of argv)
  end tell
end run
APPLESCRIPT

echo "Launched host + join clients in Terminal."
echo "Host listens on port $PORT, join target is $HOST_IP."