#!/usr/bin/env bash
# Avvia il backend + dashboard. Porta 8000, raggiungibile da telefono e Pi.
set -e
cd "$(dirname "$0")/.."
IP=$(ipconfig getifaddr en0 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}' || echo localhost)
export LEGGIMENU_BASE_URL="${LEGGIMENU_BASE_URL:-http://$IP:8000}"
echo "dashboard  -> http://localhost:8000"
echo "QR punta a -> $LEGGIMENU_BASE_URL"
exec .venv/bin/python -m uvicorn app:app --host 0.0.0.0 --port 8000 --app-dir backend "$@"
