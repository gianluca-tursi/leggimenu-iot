#!/usr/bin/env bash
# Esegue un comando che ha bisogno della porta seriale, garantendo che la
# dashboard venga chiusa prima e RIAPERTA dopo. Qualunque cosa succeda.
#
#   ./scripts/con-nodo.sh ./scripts/flash-freenove.sh firmware/radar_luce
#
# La porta seriale la puo' tenere un processo solo, e chi la libera poi si
# dimentica di rimettere su il server: e' successo piu' volte, e il dispositivo
# funzionante sembrava rotto. Il trap EXIT la riapre anche se il comando
# fallisce o viene interrotto.
set -uo pipefail
cd "$(dirname "$0")/.."

ERA_ATTIVA=0
if ./scripts/dashboard.sh stato | grep -q attiva; then ERA_ATTIVA=1; fi
./scripts/dashboard.sh ferma >/dev/null

riapri() {
  local esito=$?
  if [ "$ERA_ATTIVA" = "1" ] || [ "${SEMPRE:-0}" = "1" ]; then
    ./scripts/dashboard.sh avvia
  fi
  return $esito
}
trap riapri EXIT

"$@"
