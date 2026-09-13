# Contributing

本仓库为课程 TJU_TCP 个人实现，欢迎以 Issue / PR 形式讨论协议行为与实验复现问题。

## 开发约定

1. 在 `client` / `server` 虚拟机内编译：`cd /vagrant/tju_tcp && make && cd test && make`
2. 改动协议逻辑后，至少跑通：
   - `scripts/smoke_build.sh`（编译 + checksum）
   - 一次 `rdt_client` / `rdt_server` 无丢包短传
3. 提交信息使用英文约定式前缀：`feat:` / `fix:` / `docs:` / `chore:` / `ci:`
4. 不要提交二进制、`*.event.trace`、大体积 `logs/` 与 `.vagrant/`
5. 评测平台 IP 与 Vagrant IP 切换请用 `scripts/switch_eval_ip.sh`，勿手改后忘记还原

## 文档

- 使用说明：根目录 `README.md`
- 架构：`docs/ARCHITECTURE.md`
- 变更记录：`CHANGELOG.md`
