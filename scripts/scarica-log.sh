#!/usr/bin/env bash
# Scarica il registro dal nodo. Vedi scripts/scarica_log.py.
set -euo pipefail
cd "$(dirname "$0")/.."

# La dashboard tiene occupata la seriale. Su macOS due processi possono aprire
# la stessa porta senza errore e si rubano i byte a vicenda: il sintomo e' uno
# scarico che muore a meta' con "multiple access on port". Quindi la chiudo
# prima, come fa flash.sh, e la riapro dopo.
DASHBOARD_ERA_ATTIVA=0
if pgrep -f radar_live.py >/dev/null 2>&1; then
  DASHBOARD_ERA_ATTIVA=1
  echo "chiudo la dashboard (la riapro a fine scarico)..."
  pkill -f radar_live.py
  sleep 1.5
fi

riapri_dashboard() {
  if [ "${RIAPRI:-1}" = "1" ] && [ "$DASHBOARD_ERA_ATTIVA" = "1" ]; then
    echo "riapro la dashboard su http://localhost:8080"
    ( "$(dirname "$0")/radar-live.sh" >/tmp/radarlive.log 2>&1 & )
  fi
}
trap riapri_dashboard EXIT

.venv/bin/python scripts/scarica_log.py "$@"
