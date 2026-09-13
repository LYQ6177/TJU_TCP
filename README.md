# TJU_TCP

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

基于 UDP 的教学用简化 TCP（TJU_TCP）：在课程框架上实现连接管理、可靠传输、流量控制、基础 Reno 拥塞控制，以及选做的完整 Reno 快速恢复。

| 文档 | 说明 |
|------|------|
| [CHANGELOG](CHANGELOG.md) | 版本与协议里程碑 |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | 分层与子系统 |
| [TESTING](docs/TESTING.md) | 冒烟 / rdt / 流量 / 拥塞复现 |
| [CONTRIBUTING](CONTRIBUTING.md) | 编译、自测与提交约定 |
| [LICENSE](LICENSE) | MIT |

## 功能概览

| 模块 | 说明 | 主要代码 |
|------|------|----------|
| 连接管理 | 三次握手、四次挥手、`TIME-WAIT`、超时重传 | `tju_tcp/src/tju_tcp.c` |
| 可靠传输 | 滑动窗口、累计 ACK、RTO/Karn、快速重传 | 同上 |
| 流量控制 | 通告窗口、零窗口探测、SWS 避免 | 同上 |
| 基础 Reno | 慢启动、拥塞避免、丢包降窗，`min(rwnd,cwnd)` | `cwnd`/`ssthresh`/`reno_*` |
| 完整快速恢复 | RFC 5681 §3.2 膨胀 / dupACK 发新数据 / 收缩 | `reno_enter_fast_recovery` 等 |
| 性能实验 | 固定窗口、关 CC/FC、DELV 吞吐统计 | `TJU_FIXED_WND`、`test/run_perf.py` |

## 环境要求

- 两台 Ubuntu 20.04（Vagrant：`client` `172.17.0.2` / `server` `172.17.0.3`）
- `gcc`、`make`、`tcconfig`（`tcset`）、Python 3（`fabric`、`matplotlib`、`numpy`）
- 宿主机通过 `vagrant ssh client|server` 进入虚拟机；源码挂载于 `/vagrant/tju_tcp`

评测平台 IP 时可用脚本一键切换（勿手改后忘记还原）：

```bash
bash scripts/switch_eval_ip.sh eval      # → 172.17.0.5 / 172.17.0.6
bash scripts/switch_eval_ip.sh vagrant   # → 172.17.0.2 / 172.17.0.3
```

## 构建

宿主机/CI 冒烟：

```bash
bash scripts/smoke_build.sh
```

在虚拟机内：

```bash
cd /vagrant/tju_tcp
make                 # 生成 server / client
cd test && make      # 生成 rdt_server / rdt_client / checksum_test 等
```

拥塞实验程序（不进入官方 `test/Makefile`，避免干扰自动测试）：

```bash
gcc -pthread -g -I./inc ./test/cc_server.c -o test/cc_server ./build/*.o
gcc -pthread -g -I./inc ./test/cc_client.c -o test/cc_client ./build/*.o
# vboxsf 无执行权限时：cp 到 /tmp 再 chmod +x
```

## 运行路径

### 1. 基线握手 / 收发

```bash
# server
cd /vagrant/tju_tcp && ./server
# client
cd /vagrant/tju_tcp && ./client
```

### 2. 可靠传输（课程自动测试入口）

```bash
# 在 client 上按课程脚本执行，例如：
./test/test rdt
```

或手工：

```bash
# server
/tmp/rdt_server
# client
/tmp/rdt_client
```

### 3. 流量控制

```bash
# server
TJU_RECV_CAP=5500 /tmp/flow_server
# client
/tmp/flow_client
python3 /vagrant/tju_tcp/test/gen_graph_win.py /vagrant/tju_tcp/test/client.event.trace
```

### 4. 基础 / 完整 Reno

```bash
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite   # 可再加 --loss 2%
# server: /tmp/cc_server
# client: /tmp/cc_client 1048576
python3 /vagrant/tju_tcp/test/plot_cc.py client.event.trace tag outdir
# 或一键：python3 /vagrant/tju_tcp/test/run_fr.py
```

### 5. 官方拥塞绘图脚本

```bash
cd /vagrant/tju_tcp/test
python3 test_congestion.py 100 300 50 10
```

### 6. 性能测试（《TCP性能测试说明》）

```bash
# 仅在 client 执行
python3 /vagrant/tju_tcp/test/run_perf.py
# 结果：tju_tcp/logs/perf/
```

常用环境变量：

| 变量 | 作用 |
|------|------|
| `TJU_PERF_MODE=1` | rdt 跳过长休眠，发送后 `tju_close` |
| `TJU_PERF_SEGS=N` | 发送 N 个 1375B 段 |
| `TJU_FIXED_WND=M` | 冻结 Ws=Wr=M×MSS，并关闭 CC/FC |
| `TJU_DISABLE_CC` / `TJU_DISABLE_FC` | 单独关闭拥塞 / 流量控制 |
| `TJU_EVENT_DIR=/tmp` | event.trace 写本地盘，避免 vboxsf 拖慢 |
| `TJU_RECV_CAP` | 限制接收缓冲（流量控制实验） |
| `TJU_UDP_BUF_KB` | 覆盖内核 UDP 收发缓冲大小（KB，默认 4096） |

## 目录结构

```
tju_tcp/
  inc/          # global.h / tju_tcp.h / kernel.h / tju_packet.h
  src/          # tju_tcp.c 协议实现；kernel.c / tju_packet.c 框架
  test/         # rdt / flow / cc / 官方绘图与性能脚本
  logs/         # 本地实验备份（默认不提交）
阶段*.md        # 课程报告草稿（可填入报告模板）
Vagrantfile     # 双机实验环境
```

## event.trace

连接建立后自动写 `client.event.trace` / `server.event.trace`（可用 `TJU_EVENT_DIR` 改目录），格式：

```text
[utc_us] [EVENT] [info]
```

事件含 `SEND`/`RECV`/`CWND`/`RWND`/`SWND`/`RTTS`/`DELV`。吞吐率按接收端 `DELV`：`p = d_bits / (t2 - t1)`。

## 许可与说明

本仓库为课程实践个人实现，仅用于学习与平台质检；请勿将课程框架与隐藏测试公开传播。
