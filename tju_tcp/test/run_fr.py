# -*- coding: utf-8 -*-
"""完整 Reno 快速恢复对照：0% 回归 + 2% 丢包。在 client 上运行。"""
from __future__ import print_function
import os, shutil, time, socket
from fabric import Connection

ROOT = "/vagrant/tju_tcp"
TEST = ROOT + "/test"
LOG = ROOT + "/logs"
USER, PW = "vagrant", "vagrant"
BIN_S, BIN_C = "/tmp/cc_server", "/tmp/cc_client"
ENV = "TJU_EVENT_DIR=/tmp"

def q(conn, cmd, local=False, timeout=30):
    fn = conn.local if local else conn.run
    try:
        return fn(cmd, hide=True, warn=True, timeout=timeout, pty=False)
    except Exception as e:
        print("[warn]", cmd, e)
        return None

def kill(conn):
    q(conn, 'sudo pkill -f "/tmp/cc_server" || true', local=False)
    q(conn, 'sudo pkill -f "/tmp/cc_client" || true', local=True)
    time.sleep(0.3)

def set_net(conn, loss):
    cmd = "sudo tcset enp0s8 --rate 100Mbps --delay 20ms --loss %d%% --overwrite" % loss
    q(conn, cmd, local=False)
    q(conn, cmd, local=True)
    print("[net]", cmd)

def run_case(conn, tag, loss, nbytes=1048576, timeout=90):
    print("========", tag, "loss=%d" % loss, "========")
    kill(conn)
    q(conn, "sudo rm -f /tmp/server.event.trace /tmp/client.event.trace", local=False)
    q(conn, "sudo rm -f /tmp/client.event.trace", local=True)
    q(conn, ": > %s/server.log; : > %s/client.log" % (TEST, TEST), local=True)
    set_net(conn, loss)
    q(conn, "nohup sudo env %s %s > %s/server.log 2>&1 < /dev/null &" % (ENV, BIN_S, TEST), local=False)
    time.sleep(0.8)
    q(conn, "nohup sudo env %s %s %d > %s/client.log 2>&1 < /dev/null &" % (ENV, BIN_C, nbytes, TEST), local=True)
    t0 = time.time()
    ok = False
    while time.time() - t0 < timeout:
        r = q(conn, "tail -n 3 %s/server.log 2>/dev/null || true" % TEST, local=False)
        if r and "[CC] server got" in (r.stdout or ""):
            ok = True
            break
        time.sleep(0.4)
    time.sleep(0.4)
    kill(conn)
    q(conn, "sudo cp -f /tmp/server.event.trace %s/server.event.trace && sudo chmod 644 %s/server.event.trace" % (TEST, TEST), local=False)
    q(conn, "sudo cp -f /tmp/client.event.trace %s/client.event.trace && sudo chmod 644 %s/client.event.trace" % (TEST, TEST), local=True)
    for side in ("client", "server"):
        src = os.path.join(TEST, "%s.event.trace" % side)
        dst = os.path.join(LOG, "%s_%s.event.trace" % (tag, side))
        if os.path.isfile(src):
            shutil.copy2(src, dst)
            print("saved", dst, "bytes", os.path.getsize(dst))
        sl = os.path.join(TEST, "%s.log" % side)
        if os.path.isfile(sl):
            shutil.copy2(sl, os.path.join(LOG, "%s_%s.log" % (tag, side)))
    print("[done]", tag, "server_got" if ok else "TIMEOUT")
    return ok

def main():
    if socket.gethostname() == "server":
        print("run on client")
        return
    os.makedirs(LOG, exist_ok=True)
    with Connection(host="172.17.0.3", user=USER, connect_kwargs={"password": PW}) as conn:
        print("[build]")
        r = conn.run("cd %s && make" % ROOT, timeout=90, pty=False)
        if r.failed:
            return
        q(conn, "gcc -pthread -g -I%s/inc %s/cc_server.c -o %s/cc_server %s/build/*.o" % (ROOT, TEST, TEST, ROOT), local=False, timeout=30)
        q(conn, "gcc -pthread -g -I%s/inc %s/cc_client.c -o %s/cc_client %s/build/*.o" % (ROOT, TEST, TEST, ROOT), local=False, timeout=30)
        q(conn, "cp %s/cc_server %s && chmod +x %s" % (TEST, BIN_S, BIN_S), local=False)
        q(conn, "cp %s/cc_client %s && chmod +x %s" % (TEST, BIN_C, BIN_C), local=True)
        run_case(conn, "fr0", 0)
        run_case(conn, "fr2", 2)
        q(conn, "sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite", local=False)
        q(conn, "sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite", local=True)
        kill(conn)
    print("FR_TESTS_DONE")

if __name__ == "__main__":
    main()
