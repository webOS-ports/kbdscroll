#!/bin/sh
# Build and run kbdscroll's host tests: plain, then under ASan+UBSan, with gcc
# and with clang when they are installed. CC=... picks one compiler.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-$(mktemp -d)}
CFLAGS="-std=gnu11 -Wall -Wextra -Werror -g"
for cc in ${CC:-gcc clang}; do
	command -v "$cc" >/dev/null || { echo "$cc: not installed, skipped"; continue; }
	$cc $CFLAGS -O2 -o "$OUT/t-$cc" test_kbdscroll.c
	"$OUT/t-$cc"
	$cc $CFLAGS -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -o "$OUT/t-$cc-san" test_kbdscroll.c
	"$OUT/t-$cc-san"
done
