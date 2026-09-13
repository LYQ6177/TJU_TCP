# -*- coding: utf-8 -*-
"""第三阶段子任务二：按《TCP性能测试说明》跑两组对照实验。

必须在 client 虚拟机上执行：
    python3 /vagrant/tju_tcp/test/run_perf.py
"""
from __future__ import print_function
import csv
import os
import shutil
import socket
import sys
import time

from fabric import Connection

import plot_perf

TEST_USER = "vagrant"
TEST_PASS = "vagrant"
SERVER_HOST = "172.17.0.3"
RATE = 50
DELAY = 6
SEGS = 16384
REPEATS = 2
TIMEOUT_S = 240
ROOT = "/vagrant/tju_tcp"
TEST = ROOT + "/test"
LOGDIR = ROOT + "/logs/perf"
TRACEDIR = LOGDIR + "/traces"
BIN_C = "/tmp/rdt_client"
BIN_S = "/tmp/rdt_server"


def conn_server():
    return Connection(host=SERVER_HOST, user=TEST_USER,
                      connect_kwargs={"password": TEST_PASS})


def quiet_run(conn, cmd, local=False, timeout=30):
    fn = conn.local if local else conn.run
    try:
        return fn(cmd, hide=True, warn=True, timeout=timeout, pty=False)
    except Exception as e:
        print("[warn]", cmd, e)
        return None


def kill_all(conn):
    quiet_run(conn, 'sudo pkill -f "/tmp/rdt_server" || true', local=False)
    quiet_run(conn, 'sudo pkill -f "/tmp/rdt_client" || true', local=True)
    time.sleep(0.4)


def set_net(conn, loss):
    cmd = "sudo tcset enp0s8 --rate %dMbps --delay %dms --loss %d%% --overwrite" % (
        RATE, DELAY, loss)
    r1 = quiet_run(conn, cmd, local=False)
    r2 = quiet_run(conn, cmd, local=True)
    ok = (r1 is not None and not r1.failed) and (r2 is not None and not r2.failed)
    print("[net]", cmd, "ok" if ok else "FAIL")
    return ok


def reset_net(conn):
    cmd = "sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite"
    quiet_run(conn, cmd, local=False)
    quiet_run(conn, cmd, local=True)


def compile_and_install(conn):
    print("[build] make")
    r = conn.run("cd %s && make" % ROOT, timeout=90, pty=False)
    if r.failed:
        print("make failed")
        sys.exit(1)
    r = conn.run("cd %s && make" % TEST, timeout=90, pty=False)
    if r.failed:
        print("test make failed")
        sys.exit(1)
    for src, dst, local in (
        (TEST + "/rdt_server", BIN_S, False),
        (TEST + "/rdt_client", BIN_C, True),
    ):
        quiet_run(conn, "cp %s %s && chmod +x %s" % (src, dst, dst), local=local)
    print("[build] binaries copied to /tmp")


def still_running(conn, local, pattern):
    r = quiet_run(conn, 'pgrep -f "%s" || true' % pattern, local=local)
    if r is None:
        return True
    return bool(r.stdout.strip())


def wait_transfer(conn, timeout, expect_bytes):
    """以服务端日志交付字节为准；tju_close 可能卡在挥手，不依赖进程自行退出。"""
    t0 = time.time()
    marker = "[RDT TEST] server recv"
    remote_log = TEST + "/server.log"
    while time.time() - t0 < timeout:
        r = quiet_run(conn, "tail -n 5 %s 2>/dev/null || true" % remote_log, local=False)
        text = r.stdout if r is not None else ""
        if marker in text:
            time.sleep(0.4)
            return True
        if expect_bytes:
            tr = quiet_run(conn,
                           "sudo sh -c \"grep -c '\\[DELV\\]' /tmp/server.event.trace 2>/dev/null || echo 0\"",
                           local=False)
            try:
                n = int((tr.stdout or "0").strip().split()[-1])
            except Exception:
                n = 0
            if n * 1375 >= expect_bytes:
                time.sleep(0.4)
                return True
        time.sleep(0.4)
    return False


def start_pair(conn, env):
    kill_all(conn)
    env_s = " ".join("%s=%s" % (k, v) for k, v in env.items())
    srv = "nohup sudo env %s %s > %s/server.log 2>&1 < /dev/null &" % (env_s, BIN_S, TEST)
    cli = "nohup sudo env %s %s > %s/client.log 2>&1 < /dev/null &" % (env_s, BIN_C, TEST)
    quiet_run(conn, "sudo rm -f /tmp/server.event.trace; : > %s/server.log" % TEST, local=False)
    quiet_run(conn, "sudo rm -f /tmp/client.event.trace; : > %s/client.log" % TEST, local=True)
    rs = quiet_run(conn, srv, local=False)
    if rs is None or rs.failed:
        print("[err] start server failed")
        return False
    time.sleep(0.8)
    rc = quiet_run(conn, cli, local=True)
    if rc is None or rc.failed:
        print("[err] start client failed")
        return False
    return True


def copy_traces(conn, tag):
    os.makedirs(TRACEDIR, exist_ok=True)
    quiet_run(conn, "sudo cp -f /tmp/server.event.trace %s/server.event.trace && sudo chmod 644 %s/server.event.trace" % (TEST, TEST), local=False)
    quiet_run(conn, "sudo cp -f /tmp/client.event.trace %s/client.event.trace && sudo chmod 644 %s/client.event.trace" % (TEST, TEST), local=True)
    for side in ("client", "server"):
        src = os.path.join(TEST, "%s.event.trace" % side)
        dst = os.path.join(TRACEDIR, "%s_%s.event.trace" % (tag, side))
        if os.path.isfile(src):
            shutil.copy2(src, dst)
        else:
            print("[warn] missing", src)
    for side in ("client", "server"):
        src = os.path.join(TEST, "%s.log" % side)
        dst = os.path.join(TRACEDIR, "%s_%s.log" % (tag, side))
        if os.path.isfile(src):
            shutil.copy2(src, dst)


def measure_server(tag):
    path = os.path.join(TRACEDIR, "%s_server.event.trace" % tag)
    ts, sizes = plot_perf.parse_delv(path)
    mbps, nbytes, dt = plot_perf.avg_throughput_mbps(ts, sizes)
    return mbps, nbytes, dt, len(ts)


def run_one(conn, tag, env, loss):
    print("========", tag, env, "loss=%d" % loss, "========")
    set_net(conn, loss)
    if not start_pair(conn, env):
        copy_traces(conn, tag)
        kill_all(conn)
        return 0.0, 0, 0.0, 0
    expect = int(env.get("TJU_PERF_SEGS", str(SEGS))) * 1375
    ok = wait_transfer(conn, TIMEOUT_S, expect)
    if not ok:
        print("[timeout]", tag)
    time.sleep(0.3)
    kill_all(conn)
    copy_traces(conn, tag)
    mbps, nbytes, dt, n = measure_server(tag)
    print("[result] %s avg=%.3f Mbps bytes=%d dt=%.3fs delv=%d" % (
        tag, mbps, nbytes, dt, n))
    return mbps, nbytes, dt, n


def write_csv(path, fieldnames, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for row in rows:
            w.writerow(row)


def main():
    if socket.gethostname() == "server":
        print("只能在 client 上运行")
        sys.exit(1)
    os.makedirs(TRACEDIR, exist_ok=True)
    env_base = {
        "TJU_PERF_MODE": "1",
        "TJU_PERF_SEGS": str(SEGS),
        "TJU_EVENT_DIR": "/tmp",
    }
    with conn_server() as conn:
        compile_and_install(conn)
        wnd_rows = []
        for w in range(8, 65, 8):
            for r in range(REPEATS):
                tag = "wnd_%02d_r%d" % (w, r)
                env = dict(env_base)
                env["TJU_FIXED_WND"] = str(w)
                mbps, nbytes, dt, n = run_one(conn, tag, env, 0)
                wnd_rows.append({
                    "exp": "wnd", "wnd_mss": w, "repeat": r, "loss_pct": 0,
                    "avg_mbps": "%.4f" % mbps, "bytes": nbytes,
                    "dt_s": "%.4f" % dt, "delv": n, "tag": tag,
                })
        write_csv(os.path.join(LOGDIR, "exp1_wnd.csv"),
                  ["exp", "wnd_mss", "repeat", "loss_pct", "avg_mbps", "bytes", "dt_s", "delv", "tag"],
                  wnd_rows)

        loss_rows = []
        for loss in range(0, 7):
            for r in range(REPEATS):
                tag = "loss_%d_r%d" % (loss, r)
                env = dict(env_base)
                mbps, nbytes, dt, n = run_one(conn, tag, env, loss)
                loss_rows.append({
                    "exp": "loss", "wnd_mss": "", "repeat": r, "loss_pct": loss,
                    "avg_mbps": "%.4f" % mbps, "bytes": nbytes,
                    "dt_s": "%.4f" % dt, "delv": n, "tag": tag,
                })
        write_csv(os.path.join(LOGDIR, "exp2_loss.csv"),
                  ["exp", "wnd_mss", "repeat", "loss_pct", "avg_mbps", "bytes", "dt_s", "delv", "tag"],
                  loss_rows)

        reset_net(conn)
        kill_all(conn)

    plot_perf.summarize_dir(LOGDIR)
    print("[done] results in", LOGDIR)


if __name__ == "__main__":
    main()
