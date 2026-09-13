#!/usr/bin/env bash
# 宿主机/CI 用的快速冒烟：编译协议库与校验和自测。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/tju_tcp"
mkdir -p build
make clean >/dev/null 2>&1 || true
make
gcc -O2 -Wall -Wextra -o /tmp/tju_checksum_test test/test_checksum.c
/tmp/tju_checksum_test
echo "[smoke] build + checksum OK"
