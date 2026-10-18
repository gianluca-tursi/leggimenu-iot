#!/usr/bin/env bash
# Dice se la macchina e' pronta a far girare leggimenu, e cosa manca.
#
#   ./scripts/stato.sh
#
# Pensato per il Mac della fiera, che fa da ponte verso iot.leggimenu.it e
# dove non c'e' nessuno a cui chiedere: ogni riga dice se va o cosa fare.
# Se qualcosa non torna, manda uno screenshot.
cd "$(dirname "$0")/.."
ok()   { printf "  \033[32mOK\033[0m   %s\n" "$1"; }
no()   { printf "  \033[31mNO\033[0m   %s\n" "$1"; }
info() { printf "       %s\n" "$1"; }

echo "=== leggimenu - stato della macchina ==="
echo

echo "macOS e Python"
info "macOS $(sw_vers -productVersion 2>/dev/null)"
if command -v python3 >/dev/null 2>&1; then
  V=$(python3 -c 'import sys; print("%d.%d"%sys.version_info[:2])')
  if python3 -c 'import sys; sys.exit(0 if sys.version_info>=(3,9) else 1)'; then
    ok "Python $V"
  else
    no "Python $V - serve almeno 3.9"
    info "installa gli strumenti da riga di comando: xcode-select --install"
  fi
else
  no "Python 3 non c'e'"; info "xcode-select --install"
fi

echo
echo "Ambiente del progetto"
if [ -x .venv/bin/python ]; then
  ok "ambiente creato"
  if .venv/bin/python -c "import fastapi, uvicorn, serial, websockets" 2>/dev/null; then
    ok "librerie installate"
  else
    no "librerie mancanti o incomplete"; info "./scripts/installa.sh"
  fi
else
  no "ambiente non creato"; info "./scripts/installa.sh"
fi

echo
echo "Schede collegate"
PORTE=$(ls /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null)
if [ -n "$PORTE" ]; then
  for p in $PORTE; do ok "$p"; done
  # Se la porta c'e', il driver funziona: non ha senso dire altro.
  ok "driver della seriale funzionante"
else
  no "nessuna scheda sull'USB"
  info "attacca il Freenove (quello con i LED) al Mac"
  info "se e' attaccato e non compare, manca il driver CH340:"
  info "github.com/WCHSoftGroup/ch34xser_macos  (il .dmg, non l'App Store)"
  info "oppure il cavo USB e' solo di ricarica e non porta i dati"
fi

# Chi tiene la porta del nodo. Lo chiedo a lsof invece che a pgrep: pgrep -f
# trova anche la shell che sta eseguendo il comando con quel nome dentro.
chi_tiene() {
  for p in $PORTE; do
    for pid in $(lsof -t "$p" 2>/dev/null); do ps -o command= -p "$pid"; done
  done
}

echo
echo "Ponte"
TIENE=$(chi_tiene)
if echo "$TIENE" | grep -q ponte.py; then
  ok "il ponte e' acceso e tiene la porta del nodo"
elif echo "$TIENE" | grep -q radar_live.py; then
  no "la porta del nodo e' tenuta dalla dashboard locale"
  info "col ponte litigherebbero: ./scripts/dashboard.sh ferma"
  info "poi avvia il ponte (sotto)"
elif [ -n "$TIENE" ]; then
  no "la porta del nodo e' tenuta da un altro programma:"
  info "$TIENE"
  info "chiudilo (un monitor seriale, la IDE di Arduino...) e avvia il ponte"
elif [ -z "$PORTE" ]; then
  no "ponte spento, e manca anche il nodo (vedi sopra)"
else
  no "ponte spento"
  info "export LEGGIMENU_SEGRETO='...'     (te lo da' Gianluca)"
  info "SERVER=wss://iot.leggimenu.it ./scripts/ponte.sh"
fi

echo
echo "Server su internet"
CODICE=$(curl -s -o /dev/null -w "%{http_code}" -m 8 https://iot.leggimenu.it/ 2>/dev/null)
case "$CODICE" in
  2*|3*) ok "iot.leggimenu.it risponde"
         info "cassa: https://iot.leggimenu.it/cassa" ;;
  000)   no "iot.leggimenu.it non raggiungibile"
         info "controlla che questo Mac sia in rete (WiFi o hotspot del telefono)" ;;
  *)     no "iot.leggimenu.it risponde con errore $CODICE"
         info "il problema e' sul server, non qui: avvisa Gianluca" ;;
esac
echo
