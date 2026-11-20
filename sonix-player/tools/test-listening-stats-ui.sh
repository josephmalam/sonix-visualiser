#!/bin/bash
# Isolated Linux simulator/database regression test. Never uses a real SD card.
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
output=${SONIX_STATS_TEST_OUTPUT:-$(mktemp -d /tmp/sonix-stats-ui.XXXXXX)}
mkdir -p "$output/card"
output=$(cd "$output" && pwd)
python3 tests/create-stats-fixture.py "$output/card"
make host -j"${JOBS:-6}" > "$output/build.log" 2>&1
gcc -O2 -g -Wall -DHOST_BUILD=1 -D_FILE_OFFSET_BITS=64 -DGB_CORE=1 -DLV_CONF_INCLUDE_SIMPLE=1 -I. -Ilvgl \
    $(sdl2-config --cflags) $(pkg-config --cflags freetype2 opusfile wavpack) \
    -c tests/listeningstats-ui-test.c -o "$output/test.o"
g++ "$output/test.o" $(find build_host -name '*.o' ! -path 'build_host/gui/library/statspage.o') \
    $(find build_host -name '*.opp') -Wl,--wrap=gui_init $(sdl2-config --libs) \
    $(pkg-config --libs freetype2 opusfile wavpack) -lpthread -lm -ldl -lasound -o "$output/test"
export TZ=Europe/London SONIX_SD_ROOT="$output/card" ORIGINAL_CARD="$output/card"
export SONIX_CONFIG="$output/settings.ini" SONIX_EBOOK_CONFIG="$output/ebook.ini"
export SONIX_NO_MOUNT=1 SONIX_NO_SYSSERVER=1 SDL_AUDIODRIVER=dummy SONIX_PANEL=480x800
if [ -d "$root/../sonix-packer/assets/R1/usr/resource/sonix/language" ]; then
    export SONIX_LANG_DIR="$root/../sonix-packer/assets/R1/usr/resource/sonix/language"
else
    export SONIX_LANG_DIR="$root/usr/resource/sonix/language"
fi
printf '[ui]\nlanguage=English\n[clock]\nconfigured=1\n[power]\nscreen_off_seconds=0\n[library]\ndetect_changes=0\n' > "$SONIX_CONFIG"
Xvfb -displayfd 3 -screen 0 480x800x24 -fbdir "$output" -nolisten tcp 3> "$output/display" > "$output/xvfb.log" 2>&1 &
xvfb=$!
player=
trap 'kill "$xvfb" ${player:+"$player"} 2>/dev/null || true' EXIT
for i in $(seq 1 100); do [ -s "$output/display" ] && break; sleep .1; done
export DISPLAY=":$(cat "$output/display")"
stdbuf -oL -eL "$output/test" > "$output/player.log" 2>&1 &
player=$!
for shot in stats albums home more more-bottom stats-single stats-bottom stats-tab-loading stats-tab-reloaded; do
    seen=false
    for i in $(seq 1 300); do
        if grep -q "SHOT $shot$" "$output/player.log"; then seen=true;break;fi
        if grep -q 'Assertion.*failed\|crashed:' "$output/player.log"; then cat "$output/player.log";exit 1;fi
        sleep .1
    done
    $seen || { tail -30 "$output/player.log";exit 1; }
    sleep .5
    cp "$output/Xvfb_screen0" "$output/$shot.xwd"
done
for i in $(seq 1 300); do
    kill -0 "$player" 2>/dev/null || break
    if grep -q 'Assertion.*failed\|crashed:' "$output/player.log"; then cat "$output/player.log";exit 1;fi
    sleep .1
done
kill -0 "$player" 2>/dev/null && { tail -30 "$output/player.log";exit 1; }
wait "$player"
player=
grep 'PASS:' "$output/player.log"
printf 'Screenshots and logs: %s\n' "$output"
