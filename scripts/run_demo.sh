#!/usr/bin/env bash
set -euo pipefail

# 1) start sim
./build/feed/mdsim "ETH-USD" "tcp://*:6001" "tcp://*:6002" &
SIM_PID=$!
sleep 0.5

# 2) start pipeline
./build/app/me_pipeline &
PIPE_PID=$!

# 3) wait a bit, then post two orders to show a trade
sleep 1
./build/app/oi_cli NEW bid 200181 3 2001 || true
./build/app/oi_cli NEW ask 200181 2 3001 || true

sleep 5
kill $PIPE_PID || true
kill $SIM_PID || true
