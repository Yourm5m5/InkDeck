#!/bin/sh
# Builds main.cpp + epub.cpp against fake hardware (test/sim) and runs the scripted test.
set -e
cd "$(dirname "$0")"
SD=$(mktemp -d)
python3 make_sim_books.py "$SD" >/dev/null
# (-Wno-... : the fake String class makes g++ grumble about operator+ ambiguity; harmless)
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -g \
    -Isim -I../src sim_main.cpp ../src/main.cpp ../src/epub.cpp -lz -o /tmp/inkdeck_sim 2> /tmp/sim_build.log \
  || { grep -B2 -A6 "error" /tmp/sim_build.log | head -80; exit 1; }
grep -A3 "warning" /tmp/sim_build.log | grep -v "ambiguous\|candidate\|basic_string\|operator+\|^--" | grep "warning" || true
SDROOT="$SD" ASAN_OPTIONS=detect_leaks=0 /tmp/inkdeck_sim
