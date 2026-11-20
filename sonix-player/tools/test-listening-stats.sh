#!/bin/sh
# Pure playback sampler checks, without audio hardware or a display.
set -eu
cd "$(dirname "$0")/.."
binary=$(mktemp "${TMPDIR:-/tmp}/sonix-stats-test.XXXXXX")
trap 'rm -f "$binary"' EXIT
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -I. tests/listeningstats-playcount-test.c -pthread -o "$binary"
"$binary"
