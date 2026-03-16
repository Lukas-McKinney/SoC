#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-dev}"
OUT_DIR="${2:-dist}"
BINARY_PATH="${3:-./settlers}"

if [[ ! -f "$BINARY_PATH" ]]; then
  echo "Binary '$BINARY_PATH' not found. Build first with: make" >&2
  exit 1
fi

ARCH="$(uname -m)"
PACKAGE_NAME="SoC-${VERSION}-macos-${ARCH}"
STAGE_DIR="${OUT_DIR}/${PACKAGE_NAME}"
ZIP_PATH="${OUT_DIR}/${PACKAGE_NAME}.zip"
HASH_PATH="${OUT_DIR}/${PACKAGE_NAME}.sha256.txt"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
rm -f "$ZIP_PATH" "$HASH_PATH"

cp "$BINARY_PATH" "$STAGE_DIR/settlers"
chmod +x "$STAGE_DIR/settlers"
[[ -f README.md ]] && cp README.md "$STAGE_DIR/"
[[ -f LICENSE ]] && cp LICENSE "$STAGE_DIR/"

cat > "$STAGE_DIR/run_host.command" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
./settlers --host --player red --remote-player blue --port 24680
EOF
chmod +x "$STAGE_DIR/run_host.command"

cat > "$STAGE_DIR/run_join.command" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
HOST_IP="${1:-127.0.0.1}"
./settlers --join "$HOST_IP" --player blue --remote-player red --port 24680
EOF
chmod +x "$STAGE_DIR/run_join.command"

cat > "$STAGE_DIR/QUICKSTART.txt" <<'EOF'
SoC quick start (macOS)

1) Right click run_host.command and choose Open on one machine.
2) On the second machine, run_join.command <HOST_LAN_IP>.
3) If macOS blocks execution, run: xattr -dr com.apple.quarantine <folder>

Notes:
- Use LAN IP, not 127.0.0.1, for remote machines.
- Default port: 24680.
EOF

(
  cd "$OUT_DIR"
  zip -qr "$(basename "$ZIP_PATH")" "$(basename "$STAGE_DIR")"
)

shasum -a 256 "$ZIP_PATH" > "$HASH_PATH"

echo "Created: $ZIP_PATH"
cat "$HASH_PATH"
