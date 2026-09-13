# Testing cookbook

## Smoke (no VM)

```bash
bash scripts/smoke_build.sh
```

## Reliable transfer (Vagrant)

```bash
# server VM
cd /vagrant/tju_tcp/test && ./rdt_server

# client VM
cd /vagrant/tju_tcp/test && ./rdt_client
```

## Flow control

```bash
# server
TJU_RECV_CAP=5500 /tmp/flow_server
# client
/tmp/flow_client
```

## Congestion / fast recovery

```bash
# compile cc_* into /tmp (vboxsf may lack +x)
python3 /vagrant/tju_tcp/test/run_fr.py
```

## Official graph suite

```bash
cd /vagrant/tju_tcp/test
python3 test_congestion.py 100 300 50 10
```

## Performance

```bash
python3 /vagrant/tju_tcp/test/run_perf.py
python3 /vagrant/tju_tcp/test/plot_perf.py
```

Reset tcconfig after runs: `bash /vagrant/scripts/vm_net_reset.sh`.
