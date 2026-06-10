#!/usr/bin/env bash
find . \( -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' -o -iname '*.cc' -o -iname '*.hh' \) -print0 \
  | xargs -0 -n 1 -P "$(nproc)" clang-format -i
