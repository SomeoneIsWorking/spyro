#!/bin/sh
cd "$(dirname "$0")" || exit 1
exec uv run --frozen python bootstrap.py "$@"
