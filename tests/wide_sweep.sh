#!/bin/bash
# wide_sweep.sh - Sensitivity of the wideband path (twistd -i -t 50000):
# 5 synthetic 40 s I/Q captures at 128 kS/s per SNR, 8 classic beacons
# each, spread over +/- 42 kHz. Run from the project directory after
# "make tests". SNR in carrier-on convention (as all the test tables).
cd "$(dirname "$0")/.." || exit 1
G=./tests/gen_wide; D=./twistd
calls=(F5ZZO F6FBB ED5YAE OZ7IGY G4JNT DL0ABC K1JT EA5XYZ)
for snr in -19 -20 -21 -22 -23; do
  ok=0; tot=0; fa=0
  for sd in 1 2 3 4 5; do
    specs=()
    for i in 0 1 2 3 4 5 6 7; do
      f=$(( (i*12000 - 42000) + (sd*137 % 900) )); t0=$(echo "0.5 + $i*0.37 + $sd*0.11" | bc)
      specs+=("${calls[$i]}:$f:$t0:$snr")
    done
    SEED=$sd $G 128000 40 /tmp/twistd_ws.wav 0.5 "${specs[@]}" >/dev/null
    out=$($D -i -t 50000 -T 000000 /tmp/twistd_ws.wav 2>/dev/null)
    for i in 0 1 2 3 4 5 6 7; do tot=$((tot+1)); echo "$out" | grep -q " ${calls[$i]}\$" && ok=$((ok+1)); done
    n=$(echo "$out" | grep -c ":") ; good=$(for c in "${calls[@]}"; do echo "$out" | grep -c " $c\$"; done | paste -sd+ | bc); fa=$((fa + n - good))
  done
  echo "SNR $snr dB (carrier-on): $ok/$tot decoded, $fa false"
done
