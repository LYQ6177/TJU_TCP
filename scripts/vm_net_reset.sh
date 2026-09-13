#!/usr/bin/env bash
# 在 client/server 虚拟机上重置默认链路并打印网卡状态。
set -euo pipefail
IFACE="${1:-enp0s8}"
echo "[setup] iface=$IFACE"
sudo tcset "$IFACE" --rate 100Mbps --delay 20ms --overwrite
ip -4 addr show "$IFACE" || true
tc qdisc show dev "$IFACE" || true
echo "[setup] done"
