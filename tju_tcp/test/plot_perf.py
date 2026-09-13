# -*- coding: utf-8 -*-
"""从 server.event.trace 的 DELV 事件统计吞吐率并绘图。

平均吞吐率（TCP性能测试说明）：p = d_bits / (t2 - t1)
瞬时吞吐率：按固定间隔累加 DELV 字节。
"""
from __future__ import print_function
import os
import sys
import csv
import glob

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SMSS = 1375
RATE_MBPS = 50.0
RTT_S = 0.012  # 单程 6ms * 2


def parse_delv(path):
    ts = []
    sizes = []
    if not os.path.isfile(path):
        return ts, sizes
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if "[DELV]" not in line:
                continue
            line = line.strip()
            if not line.startswith("["):
                continue
            try:
                tpart = line.split("]")[0][1:]
                infopart = line.split("[DELV]")[-1].strip()
                if infopart.startswith("[") and infopart.endswith("]"):
                    infopart = infopart[1:-1]
                kv = {}
                for tok in infopart.replace(",", " ").split():
                    if ":" in tok:
                        k, v = tok.split(":", 1)
                        kv[k] = v
                t = int(tpart)
                sz = int(kv.get("size", "0"))
                ts.append(t)
                sizes.append(sz)
            except Exception:
                continue
    return ts, sizes


def avg_throughput_mbps(ts, sizes):
    if len(ts) < 2:
        return 0.0, 0, 0.0
    d_bits = sum(sizes) * 8.0
    dt = (ts[-1] - ts[0]) / 1e6
    if dt <= 0:
        return 0.0, sum(sizes), 0.0
    return d_bits / dt / 1e6, sum(sizes), dt


def inst_throughput(ts, sizes, intv=1.0):
    if not ts:
        return [], []
    t0 = ts[0]
    rel = [(t - t0) / 1e6 for t in ts]
    tmax = rel[-1]
    n = max(int(tmax / intv) + (1 if tmax > 0 else 0), 1)
    xs = []
    ys = []
    idx = 0
    for i in range(n):
        a = i * intv
        b = a + intv
        acc = 0
        while idx < len(rel) and rel[idx] < b:
            if rel[idx] >= a:
                acc += sizes[idx]
            idx += 1
        xs.append(a)
        ys.append(acc * 8.0 / intv / 1e6)
    return xs, ys


def theoretical_wnd_mbps(mss_count):
    tput = mss_count * SMSS * 8.0 / RTT_S / 1e6
    return min(tput, RATE_MBPS)


def plot_wnd(csv_path, out_png):
    rows = []
    with open(csv_path, "r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append(row)
    by_w = {}
    for row in rows:
        w = int(row["wnd_mss"])
        by_w.setdefault(w, []).append(float(row["avg_mbps"]))
    xs = sorted(by_w.keys())
    means = [sum(by_w[w]) / len(by_w[w]) for w in xs]
    theo = [theoretical_wnd_mbps(w) for w in xs]
    plt.figure(figsize=(7.2, 4.2))
    plt.plot(xs, means, "o-", color="black", label="measured")
    plt.plot(xs, theo, "--", color="gray", label="min(W*SMSS*8/RTT, 50Mbps)")
    plt.xlabel("Window size (MSS)")
    plt.ylabel("Throughput (Mbps)")
    plt.ylim(0, RATE_MBPS * 1.15)
    plt.grid(True, linestyle=":", alpha=0.6)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_png, dpi=200)
    plt.close()
    print("wrote", out_png)


def plot_loss(csv_path, out_png):
    rows = []
    with open(csv_path, "r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append(row)
    by_l = {}
    for row in rows:
        l = int(row["loss_pct"])
        by_l.setdefault(l, []).append(float(row["avg_mbps"]))
    xs = sorted(by_l.keys())
    means = [sum(by_l[l]) / len(by_l[l]) for l in xs]
    plt.figure(figsize=(7.2, 4.2))
    plt.plot(xs, means, "o-", color="black")
    plt.xlabel("Loss rate (%)")
    plt.ylabel("Throughput (Mbps)")
    plt.ylim(0, RATE_MBPS * 1.15)
    plt.grid(True, linestyle=":", alpha=0.6)
    plt.tight_layout()
    plt.savefig(out_png, dpi=200)
    plt.close()
    print("wrote", out_png)


def plot_inst(trace_path, out_png, title, intv=0.2):
    ts, sizes = parse_delv(trace_path)
    xs, ys = inst_throughput(ts, sizes, intv=intv)
    avg, nbytes, dt = avg_throughput_mbps(ts, sizes)
    plt.figure(figsize=(7.2, 4.2))
    if xs:
        plt.plot(xs, ys, color="black")
    plt.xlabel("Time (s)")
    plt.ylabel("Instantaneous throughput (Mbps)")
    plt.title("%s  avg=%.2f Mbps  bytes=%d  dt=%.3fs" % (title, avg, nbytes, dt))
    ymax = max(ys) * 1.15 if ys else 1
    plt.ylim(0, ymax)
    plt.grid(True, linestyle=":", alpha=0.6)
    plt.tight_layout()
    plt.savefig(out_png, dpi=200)
    plt.close()
    print("wrote", out_png, "avg", "%.3f" % avg)


def summarize_dir(perf_dir):
    wnd_csv = os.path.join(perf_dir, "exp1_wnd.csv")
    loss_csv = os.path.join(perf_dir, "exp2_loss.csv")
    if os.path.isfile(wnd_csv):
        plot_wnd(wnd_csv, os.path.join(perf_dir, "wnd_vs_tput.png"))
    if os.path.isfile(loss_csv):
        plot_loss(loss_csv, os.path.join(perf_dir, "loss_vs_tput.png"))
    traces = os.path.join(perf_dir, "traces")
    mapping = [
        ("wnd_32_r0_server.event.trace", "inst_wnd32.png", "Ws=Wr=32MSS loss=0"),
        ("loss_0_r0_server.event.trace", "inst_loss0.png", "Reno FC+CC loss=0%"),
        ("loss_6_r0_server.event.trace", "inst_loss6.png", "Reno FC+CC loss=6%"),
    ]
    for name, png, title in mapping:
        p = os.path.join(traces, name)
        if os.path.isfile(p):
            plot_inst(p, os.path.join(perf_dir, png), title, intv=0.2)


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "..", "logs", "perf")
    summarize_dir(os.path.abspath(d))
