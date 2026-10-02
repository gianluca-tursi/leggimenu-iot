#!/usr/bin/env bash
# Controlla la dashboard:  ./scripts/dashboard.sh  avvia | ferma | stato
#
# Identifica il processo con un file di PID e non con pgrep: il pattern
# "radar_live.py" matcha anche la shell che sta eseguendo il comando in cui
# quella stringa compare, quindi pgrep trovava fantasmi e pkill rischiava di
# uccidere la shell chiamante.
set -uo pipefail
cd "$(dirname "$0")/.."
PID_FILE=/tmp/leggimenu-dashboard.pid

viva() {
  [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE" 2>/dev/null)" 2>/dev/null
}

case "${1:-stato}" in
  avvia)
    if viva; then
      echo "gia' attiva (PID $(cat $PID_FILE)) su http://localhost:8080"
      exit 0
    fi
    rm -f "$PID_FILE"
    nohup .venv/bin/python tools/radar_live.py >/tmp/radarlive.log 2>&1 &
    for _ in $(seq 1 20); do sleep 0.5; viva && break; done
    if viva; then echo "dashboard avviata su http://localhost:8080"
    else echo "NON e' partita, guarda /tmp/radarlive.log"; exit 1; fi
    ;;
  ferma)
    if viva; then
      kill "$(cat $PID_FILE)" 2>/dev/null
      for _ in $(seq 1 20); do sleep 0.25; viva || break; done
      echo "dashboard fermata"
    else
      echo "non era attiva"
    fi
    rm -f "$PID_FILE"
    ;;
  stato)
    if viva; then echo "attiva (PID $(cat $PID_FILE)) su http://localhost:8080"
    else echo "ferma"; fi
    ;;
  *) echo "uso: $0 avvia|ferma|stato"; exit 2;;
esac
