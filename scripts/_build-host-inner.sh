#!/usr/bin/env bash
set -euo pipefail

# コンテナ内で実行される。直接呼ばず scripts/build-host.sh から呼び出す。
#
# 引数:
#   (なし)                          → ビルドのみ
#   run [--strategy N] [--debug]    → ビルド後 host/build/reversi_host を起動 (stdin 待ち)
#                                     N: 0=lsb (既定) 1=max_gain 2=corner (実機の DIP スイッチに当たる)
#                                     --debug で主要レジスタを stderr に毎 cycle 表示

cmake -S host -B host/build -G Ninja                                           >&2
cmake --build host/build                                                       >&2

# 先頭 "--" は docker compose 経由の常套句。あれば剥がす。
if [[ "${1:-}" == "--" ]]; then
    shift
fi

case "${1:-}" in
    "")
        ;;
    run)
        shift
        exec ./host/build/reversi_host "$@"
        ;;
    *)
        # 想定外の引数は警告して終了 (誤投入で勝手に exec しない)
        echo "Unknown arg: $1 (expected '' or 'run')" >&2
        exit 2
        ;;
esac
