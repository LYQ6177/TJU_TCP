#!/usr/bin/env bash
# 打印最近若干次 event.trace 中的 CWND 跳变，便于对照 Reno 阶段。
set -euo pipefail
trace="${1:-}"
if [[ -z "$trace" || ! -f "$trace" ]]; then
  echo "usage: $0 <client.event.trace>" >&2
  exit 2
fi
awk '
  /\[CWND\]/ {
    n=split($0, a, " ")
    ts=$1; gsub(/[\[\]]/,"",ts)
    cw=""
    for(i=1;i<=n;i++) if(a[i] ~ /^[0-9]+$/ && length(a[i])>=3) { cw=a[i]; break }
    if(cw=="") next
    if(cw!=last){ print ts, "CWND", cw; last=cw }
  }
' "$trace"
