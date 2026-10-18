#!/usr/bin/env bash
# Prende le novita' da GitHub. Pensato per il Mac della fiera, che fa solo
# da ponte fra il nodo e il server su iot.leggimenu.it.
#
#   ./scripts/aggiorna.sh
#
# NON avvia la dashboard locale: col ponte acceso litigherebbero per la porta
# USB, e su macOS due processi si dividono i byte senza dare errore.
# NON aggiorna il firmware delle schede: quello si carica dal Mac di sviluppo.
set -uo pipefail
cd "$(dirname "$0")/.."

PRIMA=$(git rev-parse HEAD)

echo "· scarico le novita'"
if ! git pull --ff-only 2>&1 | sed 's/^/     /'; then
  echo
  echo "  Il git pull non e' andato. Di solito e' perche' qui qualcuno ha"
  echo "  modificato dei file. Per buttare via le modifiche locali e"
  echo "  allinearsi a GitHub:"
  echo "     git reset --hard origin/main"
  exit 1
fi

CAMBIATI=$(git diff --name-only "$PRIMA" HEAD)

if echo "$CAMBIATI" | grep -q requirements.txt; then
  echo "· le dipendenze sono cambiate, le reinstallo"
  .venv/bin/pip install --quiet -r requirements.txt
fi

# Il ponte e' un programma che resta acceso: se il suo codice e' cambiato,
# quello che gira e' ancora il vecchio finche' non lo si rilancia.
if echo "$CAMBIATI" | grep -q -e tools/ponte.py -e scripts/ponte.sh; then
  echo
  echo "  ATTENZIONE: e' cambiato il ponte. Se sta girando, fermalo con"
  echo "  Ctrl+C nella sua finestra e rilancialo:"
  echo "     SERVER=wss://iot.leggimenu.it ./scripts/ponte.sh"
fi

echo
echo "Fatto. Controlla con:  ./scripts/stato.sh"
