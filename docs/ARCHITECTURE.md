# Architecture

## Layers

```
Application (client.c / server.c / test/*)
        │  tju_socket / bind / listen / accept / connect / send / recv / close
        ▼
TJU_TCP protocol (src/tju_tcp.c)
        │  create_packet_buf / handle_packet
        ▼
Packet + checksum (src/tju_packet.c)
        │
        ▼
UDP backend (src/kernel.c → sendToLayer3 / onTCPPocket)
```

## Subsystems in `tju_tcp.c`

| Subsystem | Responsibilities |
|-----------|------------------|
| Connection | ISN, 3WHS, teardown, TIME-WAIT |
| Reliability | send/recv buffers, ofo queue, RTO, fast retransmit |
| Flow control | `calc_adv_wnd`, peer rwnd, zero probe |
| Congestion | `cwnd`/`ssthresh`, SS/CA, full fast recovery |
| Tracing | course `event.trace` writer |

## Threads

- One receive thread (framework) demuxes UDP into `tju_handle_packet`
- Per-connection retransmit timer thread
- Per-connection send loop thread (fills `min(cwnd,rwnd)`)

## Key invariants

- In-flight bytes ≤ `min(cwnd, peer_rwnd_cap)`
- Advertised window field ≤ 65535; saturated peer rwnd may use `SEND_FLIGHT_MAX` locally
- SYN/SYN-ACK ACKs do not grow `cwnd`
