# Changelog

All notable changes to this TJU_TCP teaching implementation are documented here.

## [Unreleased]

### Planned
- NewReno partial-ACK recovery
- Optional SACK scoreboard

## [0.5.0] - 2026-09-30

### Added
- `scripts/switch_eval_ip.sh` for Vagrant ↔ eval-platform IP macros
- `scripts/smoke_build.sh` host-side compile + checksum smoke
- Standalone checksum self-test; `checksum_test` Makefile target
- `TJU_INIT_RTO_US` / `TJU_CC_DEBUG` knobs; persist-probe backoff cap
- `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `CONTRIBUTING.md`, MIT `LICENSE`, `CHANGELOG.md`

### Changed
- Enlarge UDP socket buffers; override via `TJU_UDP_BUF_KB`
- Ignore local experiment binaries under `tju_tcp/test/`

## [0.4.0] - 2026-09-30

### Added
- Full Reno fast recovery (RFC 5681 §3.2 inflate / new data / deflate)
- Performance harness (`run_perf.py` / `plot_perf.py`) for window and loss sweeps
- Official congestion graph scripts (`gen_graph_*.py`, `test_congestion.py`)
- Root README with build/run paths

### Changed
- Makefile creates `build/` before compiling objects

## [0.3.0] - 2026-09-17

### Added
- Basic Reno: slow start, congestion avoidance, RTO and 3-dupACK cut
- Course `event.trace` (SEND/RECV/CWND/RWND/SWND/RTTS/DELV)
- Congestion and flow-control experiment programs

## [0.2.0] - 2026-09-08

### Added
- Reliable transfer: sliding window, cumulative ACK, RTO/Karn, fast retransmit
- Flow control: advertised window, zero-window probe, SWS avoidance

## [0.1.0] - 2026-09-02

### Added
- Connection establish/close state machine on course UDP framework
- Vagrant dual-VM baseline environment
