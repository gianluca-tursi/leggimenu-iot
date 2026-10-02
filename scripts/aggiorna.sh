#!/usr/bin/env bash
# Prende le novita' da GitHub e rimette su il server.
#
#   ./scripts/aggiorna.sh
#
# E' l'unico comando che serve sul Mac della fiera dopo che qualcosa e' stato
# sviluppato altrove. NON aggiorna il firmware delle schede: quello si carica
# dal Mac di sviluppo, perche' qui Arduino non c'e'.
set -uo pipefail
cd "$(dirname "$0")/.."

echo "· fermo il server"
./scripts/dashboard.sh ferma >/dev/null 2>&1

echo "· scarico le novita'"
if ! git pull --ff-only 2>&1 | sed 's/^/     /'; then
  echo
  echo "  Il git pull non e' andato. Di solito e' perche' qui qualcuno ha"
  echo "  modificato dei file. Per buttare via le modifiche locali e"
  echo "  allinearsi a GitHub:"
  echo "     git reset --hard origin/main"
  exit 1
fi

if git diff --name-only HEAD@{1} HEAD 2>/dev/null | grep -q requirements.txt; then
  echo "· le dipendenze sono cambiate, le reinstallo"
  .venv/bin/pip install --quiet -r requirements.txt
fi

echo "· riavvio il server"
./scripts/dashboard.sh avvia
echo
echo "Fatto. Controlla con:  ./scripts/stato.sh"
