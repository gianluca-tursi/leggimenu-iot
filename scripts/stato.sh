#!/usr/bin/env bash
# Dice se la macchina e' pronta a far girare leggimenu, e cosa manca.
#
#   ./scripts/stato.sh
#
# Pensato per il Mac della fiera, dove non c'e' nessuno a cui chiedere: ogni
# riga dice se va o cosa fare. Se qualcosa non torna, manda uno screenshot.
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

echo
echo "Server"
if ./scripts/dashboard.sh stato 2>/dev/null | grep -q attiva; then
  ok "$(./scripts/dashboard.sh stato)"
else
  no "server fermo"; info "./scripts/dashboard.sh avvia"
fi

IP=$(ifconfig 2>/dev/null | awk '/inet /{if($2!="127.0.0.1"){print $2; exit}}')
echo
echo "Rete"
if [ -n "$IP" ]; then
  ok "questo Mac e' $IP"
  info "dal telefono:  http://$IP:8080"
  info "IMPORTANTE: apri la dashboard, scheda Menu e WiFi,"
  info "e premi 'Aggiorna i QR' - il QR sul pannello deve puntare qui."
else
  no "nessuna rete"; info "collega il Mac al WiFi"
fi
echo
