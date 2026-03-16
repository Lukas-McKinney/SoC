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

if command -v otool >/dev/null 2>&1 && command -v install_name_tool >/dev/null 2>&1; then
  LIB_DIR="$STAGE_DIR/lib"
  mkdir -p "$LIB_DIR"

  DEPS_FILE="$OUT_DIR/.macos_deps_${PACKAGE_NAME}.txt"
  > "$DEPS_FILE"

  is_system_dep() {
    local dep="$1"
    [[ "$dep" == /usr/lib/* || "$dep" == /System/Library/* ]]
  }

  collect_abs_deps() {
    local file="$1"
    otool -L "$file" \
      | tail -n +2 \
      | awk '{print $1}' \
      | while IFS= read -r dep; do
          [[ -z "$dep" ]] && continue
          if is_system_dep "$dep"; then
            continue
          fi
          if [[ "$dep" == /* && -f "$dep" ]]; then
            echo "$dep"
          fi
        done
  }

  add_dep() {
    local dep="$1"
    if ! grep -Fxq "$dep" "$DEPS_FILE"; then
      echo "$dep" >> "$DEPS_FILE"
    fi
  }

  collect_abs_deps "$STAGE_DIR/settlers" | while IFS= read -r dep; do
    add_dep "$dep"
  done

  changed=1
  while [[ $changed -eq 1 ]]; do
    changed=0
    while IFS= read -r dep; do
      [[ -z "$dep" ]] && continue
      base="$(basename "$dep")"
      staged="$LIB_DIR/$base"
      if [[ ! -f "$staged" ]]; then
        cp -L "$dep" "$staged"
        chmod +w "$staged" || true
        changed=1
      fi
      collect_abs_deps "$staged" | while IFS= read -r nested; do
        add_dep "$nested"
      done
    done < "$DEPS_FILE"
  done

  while IFS= read -r dep; do
    [[ -z "$dep" ]] && continue
    base="$(basename "$dep")"
    install_name_tool -change "$dep" "@loader_path/lib/$base" "$STAGE_DIR/settlers" || true
  done < "$DEPS_FILE"

  while IFS= read -r dep; do
    [[ -z "$dep" ]] && continue
    base="$(basename "$dep")"
    staged="$LIB_DIR/$base"
    install_name_tool -id "@loader_path/$base" "$staged" || true
    while IFS= read -r inner; do
      [[ -z "$inner" ]] && continue
      if is_system_dep "$inner"; then
        continue
      fi
      inner_base="$(basename "$inner")"
      install_name_tool -change "$inner" "@loader_path/$inner_base" "$staged" || true
    done < <(otool -L "$staged" | tail -n +2 | awk '{print $1}')
  done < "$DEPS_FILE"

  rm -f "$DEPS_FILE"
fi

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
