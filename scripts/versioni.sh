#!/usr/bin/env bash
# Dice se le schede hanno il firmware che e' nel progetto, o se sono indietro.
#
#   ./scripts/versioni.sh
#
# Una modifica tocca spesso nodo, pannello e server insieme: se ne carico due e
# il terzo resta fermo, il guasto si scopre provando - ed e' successo due volte.
# Confronto l'impronta dei sorgenti con quella annotata all'ultimo caricamento.
set -uo pipefail
cd "$(dirname "$0")/.."

impronta() {
  find "$1" -type f \( -name '*.ino' -o -name '*.c' -o -name '*.cpp' -o -name '*.h' \) \
    -exec shasum -a 256 {} + 2>/dev/null | sort | shasum -a 256 | cut -c1-12
}

controlla() {
  local sketch="$1" nome; nome=$(basename "$sketch")
  printf "  %-12s " "$nome"
  [ -d "$sketch" ] || { echo "non c'e'"; return; }
  local ora prima
  ora=$(impronta "$sketch")
  prima=$(cat ".firmware-caricati/$nome" 2>/dev/null || echo "")
  if [ -z "$prima" ]; then
    echo "mai caricato da qui (o impronta persa)"
  elif [ "$ora" = "$prima" ]; then
    echo "aggiornato"
  else
    echo "INDIETRO — il codice e' cambiato dopo l'ultimo caricamento"
  fi
}

echo "firmware delle schede:"
controlla firmware/radar_luce
controlla firmware/pannello42
echo
echo "(l'impronta si aggiorna da sola quando carichi con gli script)"
