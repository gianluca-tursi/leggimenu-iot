#!/usr/bin/env bash
# Pubblica sul server: copia i file e tocca RICARICA.
#
#   ./scripts/pubblica.sh
#
# Nessun riavvio da root: il guardiano dentro l'applicazione vede RICARICA
# cambiare e chiede a gunicorn di ricambiare i worker col codice nuovo.
# Le connessioni in corso finiscono in pace.
set -euo pipefail
cd "$(dirname "$0")/.."

UTENTE="${UTENTE:-tavolo}"
HOST="${HOST:-65.21.93.36}"
CHIAVE="${CHIAVE:-$HOME/.ssh/leggimenu_tavolo}"
REMOTA="${REMOTA:-/home/$UTENTE/app}"

[ -f "$CHIAVE" ] || { echo "manca la chiave $CHIAVE"; exit 1; }

echo "· copio su $UTENTE@$HOST:$REMOTA"
# Solo quello che serve far girare: niente ambiente Python, niente firmware,
# niente registrazioni. Il server non compila sketch e non legge radar.
rsync -az --delete -e "ssh -i $CHIAVE -o StrictHostKeyChecking=accept-new" \
  --include='tools/***' \
  --include='backend/***' \
  --include='requirements.txt' \
  --exclude='*' \
  ./ "$UTENTE@$HOST:$REMOTA/"

echo "· tocco RICARICA"
ssh -i "$CHIAVE" "$UTENTE@$HOST" "touch $REMOTA/RICARICA"

echo "· aspetto che i worker nuovi rispondano"
sleep 4
ssh -i "$CHIAVE" "$UTENTE@$HOST" \
  "curl -s -o /dev/null -w 'in locale sul server: %{http_code}\n' http://127.0.0.1:8099/cassa"
