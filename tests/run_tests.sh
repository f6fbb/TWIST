#!/bin/sh
# run_tests.sh - self-checking test suite (make test). Exit status 0 = all passed.
# Run from the repository root after "make test" has built the programs.
T=${TMPDIR:-/tmp}
fail=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; fail=1; }
expect() {  # expect "name" "output" "pattern"
    if printf '%s\n' "$2" | grep -q -- "$3"; then ok "$1"; else bad "$1"; printf '%s\n' "$2" | sed 's/^/      /'; fi
}

# 1. classic encoder: bit-exact against the three reference transmissions
if ./tests/test_encoder >/dev/null; then ok "classic encoder bit-exact"; else bad "classic encoder bit-exact"; fi

# 2. real off-air recording (classic frame, F5ZZO): operad, not twistd
out=$(./operad -T 120000 tests/real/f5zzo.wav 2>&1)
expect "operad: real recording F5ZZO" "$out" "1564 .* 05 : F5ZZO"
out=$(./twistd -T 120000 tests/real/f5zzo.wav 2>&1)
if printf '%s\n' "$out" | grep -q " : "; then bad "twistd: the classic frame is not its own"; else ok "twistd: the classic frame is not its own"; fi

# 3. classic + TWIST S + TWIST L in one SSB recording: each decoder its own
./tests/gen_test_wav F5ZZO,TWS:F6FBB,TWL:F1HDI:JN18cs 0.5 -18 1520 1.5 120 12000 "$T/tw_t1.wav" >/dev/null
out=$(./operad -T 120000 "$T/tw_t1.wav" 2>&1)
expect "SSB: classic frame (operad)"  "$out" " 05 : F5ZZO"
n=$(printf '%s\n' "$out" | grep -c " : ")
[ "$n" -eq 1 ] && ok "operad: the classic frame only" || bad "operad: $n spots instead of 1"
out=$(./twistd -T 120000 "$T/tw_t1.wav" 2>&1)
expect "SSB: TWIST S (twistd)"        "$out" "TWIST S : F6FBB"
expect "SSB: TWIST L (twistd)"        "$out" "TWIST L : F1HDI JN18CS"
n=$(printf '%s\n' "$out" | grep -c " : ")
[ "$n" -eq 2 ] && ok "twistd: the TWIST frames only" || bad "twistd: $n spots instead of 2"
out=$(./twistd -p classic -T 120000 "$T/tw_t1.wav" 2>&1)
expect "twistd -p classic: sends to operad" "$out" "operad"

# 4. I/Q input, narrowband
./tests/gen_test_wav TWL:EA5XYZ:IM98WL 0.5 -17 -2500 3.2 48 24000 "$T/tw_t2.wav" iq >/dev/null
out=$(./twistd -i -t 3000 -T 120000 "$T/tw_t2.wav" 2>&1)
expect "I/Q: TWIST L" "$out" "TWIST L : EA5XYZ IM98WL"

# 5. wideband scan, three frame types and a steady carrier
./tests/gen_wide 64000 50 "$T/tw_t3.wav" 0.5 F5ZZO:-21000:2.0:-16 TWS/F6FBB:17300:4.0:-17 TWL/F1HDI/JN18cs:-3000:1.0:-17 CW:5000:-10 >/dev/null
out=$(./twistd -i -t 30000 -T 120000 "$T/tw_t3.wav" 2>&1; ./operad -i -t 30000 -T 120000 "$T/tw_t3.wav" 2>&1)
expect "wideband: classic frame" "$out" "21000.* 05 : F5ZZO"
expect "wideband: TWIST S"       "$out" "17299.* TWIST S : F6FBB"
expect "wideband: TWIST L"       "$out" "2999.* TWIST L : F1HDI JN18CS"
n=$(printf '%s\n' "$out" | grep -c " : ")
[ "$n" -eq 3 ] && ok "wideband: no false decode" || bad "wideband: $n spots instead of 3"

# 5b. SNR of strong signals, with and without a receiver AGC
out=$(./tests/snr_agc 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  snr: /; s/^FAIL  /FAIL  snr: /'
[ $rc -eq 0 ] || bad "snr: tests/snr_agc (exit status $rc)"

# 5c. several decoders at once in several threads (FFTW planner lock)
out=$(./tests/test_threads 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  threads: /; s/^FAIL  /FAIL  threads: /'
[ $rc -eq 0 ] || bad "threads: tests/test_threads (exit status $rc)"

# 5d. deep search next to a strong signal (no ghost of a similar callsign)
out=$(./tests/deep_shadow 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  deep: /; s/^FAIL  /FAIL  deep: /'
[ $rc -eq 0 ] || bad "deep: tests/deep_shadow (exit status $rc)"

# 5e. PI4: pi4d on a real recording of F5ZZO (its own output, as before),
#     the PI4 pipeline of libpi4 on synthetic frames
out=$(./pi4d -T 120000 tests/real/pi4_f5zzo.wav 2>&1)
expect "pi4d: real recording F5ZZO" "$out" "120000 -11  1.4  798 : F5ZZO"
expect "pi4d: real recording, the /JN12AX of the next minute" "$out" "61.4  798 : _/JN12AX"
out=$(./twistd -T 120000 tests/real/pi4_f5zzo.wav 2>&1; ./operad -T 120000 tests/real/pi4_f5zzo.wav 2>&1)
if printf '%s\n' "$out" | grep -q " : "; then bad "PI4 not decoded by twistd / operad"; printf '%s\n' "$out" | sed 's/^/      /'; else ok "PI4 not decoded by twistd / operad"; fi
out=$(./tests/pi4_test 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  pi4: /; s/^FAIL  /FAIL  pi4: /'
[ $rc -eq 0 ] || bad "pi4: tests/pi4_test (exit status $rc)"

# 6. beacon encoder (firmware/) identical to twistenc
for f in F6FBB F5ZZO:JN12AX EA5XYZ:IM98wl 9A1A K1JT:FN20 OZ7IGY:JO65ab; do
    call=${f%%:*}; loc=""; [ "$call" != "$f" ] && loc=${f#*:}
    a=$(./twistenc $call $loc | sed -n 's/^chips *: //p')
    b=$(./firmware/test_twist_encoder "$f" | sed 's/^[^:]*:[^:]*: //; s/^[^ ]*: //')
    [ -n "$a" ] && [ "$a" = "$b" ] && ok "firmware encoder = twistenc ($f)" || bad "firmware encoder = twistenc ($f)"
done

# 7. independent Python encoder written from PROTOCOL.md (if python3 is there)
if command -v python3 >/dev/null 2>&1; then
    if python3 tools/twist_ref.py >/dev/null; then ok "PROTOCOL.md reference encoder"; else bad "PROTOCOL.md reference encoder"; fi
fi

# 8. files over 2 GiB: the data chunk starts past 2 GiB (32-bit "long"
#    limit on Windows), after a large JUNK chunk. Sparse on Linux/macOS,
#    about 2.1 GB of real disk on Windows. TWIST_SKIP_BIG=1 skips it.
if [ "${TWIST_SKIP_BIG:-0}" = 1 ]; then
    echo "SKIP  files over 2 GiB (TWIST_SKIP_BIG=1)"
else
    if ./tests/pad_riff "$T/tw_t1.wav" "$T/tw_big1.wav" 2100 >/dev/null; then
        out=$(./twistd -T 120000 "$T/tw_big1.wav" 2>&1)
        expect "WAV over 2 GiB: TWIST L" "$out" "TWIST L : F1HDI JN18CS"
    else bad "WAV over 2 GiB: cannot create the file"; fi
    rm -f "$T/tw_big1.wav"
    if ./tests/pad_riff "$T/tw_t2.wav" "$T/tw_big2.wav" 2100 rf64 >/dev/null; then
        out=$(./twistd -i -t 3000 -T 120000 "$T/tw_big2.wav" 2>&1)
        expect "RF64 I/Q over 2 GiB: TWIST L" "$out" "TWIST L : EA5XYZ IM98WL"
    else bad "RF64 I/Q over 2 GiB: cannot create the file"; fi
    rm -f "$T/tw_big2.wav"
fi

# 9. the shared libraries (twcore.h, libtwist.h, twcl.h, libpi4.h), as the
#    desktop application uses them: short pass on a 45 s block, each
#    library alone, arbitration, encoders, errors. PATH: Windows looks
#    for the DLLs there.
out=$(PATH="$PWD:$PATH" ./tests/test_libs 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL|      )" | sed 's/^PASS  /PASS  libs: /; s/^FAIL  /FAIL  libs: /'
[ $rc -eq 0 ] || fail=1
# each protocol library depends on no other (Linux: its dynamic section)
if command -v readelf >/dev/null 2>&1 && [ -f libtwist.so ]; then
    if readelf -d libtwist.so libopera.so libpi4.so | grep NEEDED | grep -q -E "libtwcore|libtwist|libopera|libpi4"; then
        bad "libraries: a protocol library needs another"; readelf -d libtwist.so libopera.so libpi4.so | grep NEEDED | sed 's/^/      /'
    else ok "libraries: each protocol library stands alone"; fi
fi

# 10. transmit audio (twl_tx_*): rendered, decoded back, chip timing at
#     44.1 kHz, no clicks, immediate stop
out=$(PATH="$PWD:$PATH" ./tests/test_tx 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  tx: /; s/^FAIL  /FAIL  tx: /'
[ $rc -eq 0 ] || fail=1

# 11. twistenc -w: the WAV of a frame decodes with twistd
./twistenc -w "$T/tw_tx.wav" -r 44100 -f 1200 F6FBB JN12AX >/dev/null
out=$(./twistd -f 1200 -T 120000 "$T/tw_tx.wav" 2>&1)
expect "twistenc -w: WAV decodes" "$out" "1.0 1200 TWIST L : F6FBB JN12AX"

# 12. live audio capture path (twl_audio_*), fed without a sound card
out=$(PATH="$PWD:$PATH" ./tests/test_audio 2>&1); rc=$?
printf '%s\n' "$out" | grep -E "^(PASS|FAIL)" | sed 's/^PASS  /PASS  audio: /; s/^FAIL  /FAIL  audio: /'
[ $rc -eq 0 ] || fail=1

# 13. twistlive, the continuous decoder, on the SSB recording of test 3
#     replayed through the capture path: each frame printed exactly once
out=$(./twistlive -i "$T/tw_t1.wav" -F -T 120000 2>&1)
expect "twistlive: classic frame" "$out" "12:00:01.5 .* 05 : F5ZZO"
expect "twistlive: TWIST S"       "$out" "12:00:34.1 .* TWIST S : F6FBB"
expect "twistlive: TWIST L"       "$out" "12:01:06.7 .* TWIST L : F1HDI JN18CS"
n=$(printf '%s\n' "$out" | grep -c " : ")
[ "$n" -eq 3 ] && ok "twistlive: each frame once" || bad "twistlive: $n spots instead of 3"

[ $fail -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
