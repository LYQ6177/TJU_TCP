#!/bin/bash
set -e
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite
/tmp/cc_client 1048576 &
CPID=$!
sleep 0.5
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --loss 100% --overwrite
sleep 1.0
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --overwrite
wait $CPID || true
echo CLIENT_DONE
