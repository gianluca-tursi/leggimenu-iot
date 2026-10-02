#!/usr/bin/env bash
# Apre un indirizzo pubblico verso il server, perche' in fiera il telefono del
# visitatore e' su rete mobile e la rete del locale non la vede.
#
#   ./scripts/tunnel.sh
#
# Scarica cloudflared la prima volta, poi lo riusa. Nessun account: in cambio
# l'indirizzo cambia a ogni avvio, quindi dopo averlo lanciato va rimesso nel
# QR dalla dashboard ("Aggiorna i QR").
#
# Dal mondo passano solo il menu e l'invio dell'ordine: cassa e comandi
# restano raggiungibili solo dalla rete del locale.
set -uo pipefail
cd "$(dirname "$0")/.."
PORTA="${PORTA:-8080}"

if [ ! -x bin/cloudflared ]; then
  case "$(uname -m)" in arm64) A=arm64;; *) A=amd64;; esac
  echo "· scarico cloudflared (una volta sola)"
  mkdir -p bin
  curl -sL --max-time 180 \
    "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-$A.tgz" \
    -o /tmp/cf.tgz || { echo "  scarico fallito: serve internet"; exit 1; }
  tar xzf /tmp/cf.tgz -C bin && chmod +x bin/cloudflared
fi

if ! curl -s -o /dev/null -m 3 "http://localhost:$PORTA/"; then
  echo "Il server non risponde sulla $PORTA. Avvialo prima:"
  echo "   ./scripts/dashboard.sh avvia"
  exit 1
fi

echo "· apro il tunnel (Ctrl-C per chiuderlo)"
echo "  tengo sveglio il Mac finche' resta aperto: se si addormenta il tunnel"
echo "  cade e il QR sul tavolo smette di funzionare senza avvisare nessuno."
echo

# caffeinate -i: niente sonno per inattivita' finche' cloudflared gira.
# Non uso -s perche' vale solo a corrente attaccata, e in fiera non e' detto.
exec caffeinate -i ./bin/cloudflared tunnel --no-autoupdate --url "http://localhost:$PORTA"
