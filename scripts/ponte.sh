#!/usr/bin/env bash
# Collega il nodo attaccato qui via USB al server su internet.
#
#   ./scripts/ponte.sh
#   SERVER=wss://tavolo.esempio.it ./scripts/ponte.sh
#
# Tiene sveglio il Mac: se si addormenta il tavolo sparisce dalla cassa.
set -uo pipefail
cd "$(dirname "$0")/.."
exec caffeinate -i .venv/bin/python tools/ponte.py
