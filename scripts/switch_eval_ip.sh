#!/usr/bin/env bash
# 在评测平台 IP 与本地 Vagrant IP 之间切换 global.h 宏。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HDR="$ROOT/tju_tcp/inc/global.h"
mode="${1:-}"
if [[ "$mode" != "vagrant" && "$mode" != "eval" ]]; then
  echo "usage: $0 vagrant|eval" >&2
  exit 2
fi
if [[ "$mode" == "vagrant" ]]; then
  cip="172.17.0.2"; sip="172.17.0.3"
else
  cip="172.17.0.5"; sip="172.17.0.6"
fi
# 仅替换宏定义行，保留注释说明
tmp="$(mktemp)"
awk -v cip="$cip" -v sip="$sip" '
  /^#define TJU_CLIENT_IP / { print "#define TJU_CLIENT_IP \"" cip "\""; next }
  /^#define TJU_SERVER_IP / { print "#define TJU_SERVER_IP \"" sip "\""; next }
  { print }
' "$HDR" > "$tmp"
mv "$tmp" "$HDR"
echo "[ok] TJU_CLIENT_IP=$cip TJU_SERVER_IP=$sip"
grep -E 'TJU_(CLIENT|SERVER)_IP' "$HDR"
