#!/usr/bin/env bash
# Prepara una macchina nuova a far girare il server leggimenu.
#
#   ./scripts/installa.sh
#
# Non serve Arduino: le schede hanno gia' il loro firmware. Questo installa
# solo quello che serve al Mac per fare da server e parlare col nodo via USB.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "· creo l'ambiente Python"
python3 -m venv .venv
.venv/bin/pip install --quiet --upgrade pip
echo "· installo le dipendenze"
.venv/bin/pip install --quiet -r requirements.txt

echo
echo "Fatto. Prima di avviare, due cose da controllare:"
echo
echo "  1. il driver della seriale CH340, altrimenti il Mac non vede le schede:"
echo "     https://github.com/WCHSoftGroup/ch34xser_macos  (CH34xVCPDriver, .dmg)"
echo
echo "  2. attacca il Freenove via USB, poi:"
echo "     ./scripts/dashboard.sh avvia"
echo
