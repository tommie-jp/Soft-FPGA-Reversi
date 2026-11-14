#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<'EOF'
使い方:
  scripts/doBuildPico.sh           # バージョン更新 + ビルドのみ
  scripts/doBuildPico.sh --flash   # ビルド後、Debug Probe 経由で書き込む

ビルド成果物:
  firmware/build/firmware.uf2 / firmware.elf
    3 つの着手の方式 (lsb / max_gain / corner) をすべて含む。
    起動時 (リセット時) に DIP スイッチ GP10〜GP13 を読んで選ぶ:
      0 (すべて OFF) = lsb       行優先最初の合法手
      1 (4 番 ON)    = max_gain  反転駒数最大の合法手
      2 (3 番 ON)    = corner    角優先
      それ以外       = lsb
    1 番 (GP10) が最上位ビット。ON が 1。
    対局の外なら、シリアルから XS<d> で選び直せる (次のリセットまで有効)。

BOOTSEL で書き込む場合:
  1. Pico 2 の BOOTSEL ボタンを押しながら USB 接続
  2. RPI-RP2350 ドライブに firmware/build/firmware.uf2 をドラッグ＆ドロップ
EOF
}

FLASH_ELF=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage; exit 0 ;;
        --flash)
            FLASH_ELF="firmware/build/firmware.elf"
            shift
            ;;
        *)
            echo "Unknown option: '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

cd "${SCRIPT_DIR}"
./doVersionUp.sh
./build-pico.sh

if [[ -n "${FLASH_ELF}" ]]; then
    ./flash.sh "${FLASH_ELF}"
fi
