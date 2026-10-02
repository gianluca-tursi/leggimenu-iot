#!/usr/bin/env bash
# Avvia un nodo tavolo finto sul Mac: disegna su /tmp/leggimenu-eink.png
set -e
cd "$(dirname "$0")/.."
exec .venv/bin/python device/agent.py --tavolo "${1:-T07}" --server "${2:-http://localhost:8000}"
