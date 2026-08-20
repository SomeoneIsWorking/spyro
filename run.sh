#!/usr/bin/env bash
set -eu
cd -- "$(dirname -- "$0")"
exec python3 tools/run.py "$@"
