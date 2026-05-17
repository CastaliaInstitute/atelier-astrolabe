#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export ASTROLABE_ROOT="$(cd "$DIR/../.." && pwd)"
exec "$DIR/.venv/bin/python" "$DIR/server.py"
