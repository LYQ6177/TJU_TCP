#!/usr/bin/env python3
# 从 client.event.trace / server.event.trace 绘制基础 Reno 窗口曲线。
import os
import re
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("need matplotlib")
    sys.exit(1)

EVENT_RE = re.compile(
    r"^\[(\d+)\] \[([A-Z]+)\] \[(.*)\]\s*$"
)

def parse(path):
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = EVENT_RE.match(line.strip())
            if not m:
                continue
            ts, ev, info = m.group(1), m.group(2), m.group(3)
            rec = {"ts": int(ts), "ev": ev}
            for kv in info.split():
                if ":" in kv:
                    k, v = kv.split(":", 1)
                    rec[k] = v
            rows.append(rec)
    return rows

def rel_ms(rows):
    if not rows:
        return []
    t0 = rows[0]["ts"]
    out = []
    for r in rows:
        d = dict(r)
        d["t"] = (r["ts"] - t0) / 1000.0
        out.append(d)
    return out

def plot_windows(rows, out_png, title):
    cwnd = [(r["t"], int(r["size"]), int(r.get("type", "0"))) for r in rows if r["ev"] == "CWND"]
    swnd = [(r["t"], int(r["size"])) for r in rows if r["ev"] == "SWND"]
    rwnd = [(r["t"], int(r["size"])) for r in rows if r["ev"] == "RWND"]
    send = [r for r in rows if r["ev"] == "SEND" and int(r.get("length", "0")) > 0]

    fig, ax = plt.subplots(figsize=(10, 4.2))
    if cwnd:
        ax.step([x[0] for x in cwnd], [x[1] for x in cwnd], where="post", label="cwnd", lw=1.6)
    if swnd:
        ax.step([x[0] for x in swnd], [x[1] for x in swnd], where="post", label="swnd=min(cwnd,rwnd)", lw=1.2)
    if rwnd:
        ax.step([x[0] for x in rwnd], [x[1] for x in rwnd], where="post", label="rwnd(local)", lw=1.0, alpha=0.7)

    marks = {0: "SS", 1: "CA", 2: "FastRecovery", 3: "RTO"}
    seen = set()
    for t, sz, typ in cwnd:
        if typ in (2, 3):
            lab = marks[typ] if typ not in seen else None
            seen.add(typ)
            ax.scatter([t], [sz], marker="x" if typ == 3 else "o", s=36,
                       color="crimson" if typ == 3 else "darkorange", label=lab, zorder=5)

    ax.set_xlabel("time (ms)")
    ax.set_ylabel("window (bytes)")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(out_png, dpi=140)
    plt.close(fig)
    print("wrote", out_png, "cwnd", len(cwnd), "swnd", len(swnd), "data_send", len(send))

def plot_seq(rows, out_png, title):
    send = [r for r in rows if r["ev"] == "SEND"]
    recv = [r for r in rows if r["ev"] == "RECV"]
    fig, ax = plt.subplots(figsize=(10, 4.2))
    if send:
        ax.scatter([r["t"] for r in send], [int(r.get("seq", "0")) for r in send],
                   s=8, label="SEND seq", alpha=0.6)
    if recv:
        ax.scatter([r["t"] for r in recv], [int(r.get("ack", "0")) for r in recv],
                   s=8, label="RECV ack", alpha=0.5, marker=".")
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("sequence / ack")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(out_png, dpi=140)
    plt.close(fig)
    print("wrote", out_png)

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "client.event.trace"
    tag = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(os.path.basename(src))[0]
    outdir = sys.argv[3] if len(sys.argv) > 3 else os.path.dirname(os.path.abspath(src)) or "."
    rows = rel_ms(parse(src))
    if not rows:
        print("no events in", src)
        sys.exit(1)
    plot_windows(rows, os.path.join(outdir, tag + "_wnd.png"), tag + " windows")
    plot_seq(rows, os.path.join(outdir, tag + "_seq.png"), tag + " seq/ack")

if __name__ == "__main__":
    main()
