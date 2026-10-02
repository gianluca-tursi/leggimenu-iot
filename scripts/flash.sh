#!/usr/bin/env bash
# Compila e carica uno sketch sul CrowPanel usando l'arduino-cli gia' dentro
# Arduino IDE: nessuna installazione, nessuna finestra da cliccare.
#
#   ./scripts/flash.sh                      -> compila e carica test_radar
#   ./scripts/flash.sh firmware/altro       -> carica un altro sketch
#   ./scripts/flash.sh --monitor            -> carica e apre il monitor seriale
set -e
cd "$(dirname "$0")/.."

CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
# CDCOnBoot=default: su questa scheda la USB passa da un CH340 su UART0,
# non dalla USB nativa dell'S3. Con "cdc" il monitor resterebbe muto.
# UploadSpeed=115200: il default e' 921600, che il ponte CH340 di questa scheda
# non regge in modo affidabile ("Failed to write to target RAM"). Piu' lento ma
# non fallisce. Si puo' forzare con:  VELOCITA=921600 ./scripts/flash.sh
VELOCITA="${VELOCITA:-115200}"
FQBN="${FQBN_OVERRIDE:-esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB,USBMode=hwcdc,UploadSpeed=$VELOCITA}"

SKETCH="firmware/test_radar"
MONITOR=0
for arg in "$@"; do
  case "$arg" in
    --monitor) MONITOR=1 ;;
    *) SKETCH="$arg" ;;
  esac
done

[ -x "$CLI" ] || { echo "arduino-cli non trovato dentro Arduino IDE.app"; exit 1; }

# La dashboard tiene occupata la seriale: la chiudo prima di caricare e la
# riapro dopo. Senza questo, o il caricamento fallisce, o resta chiusa e
# sembra che sia caduta la connessione.
DASHBOARD_ERA_ATTIVA=0
if pgrep -f radar_live.py >/dev/null 2>&1; then
  DASHBOARD_ERA_ATTIVA=1
  echo "chiudo la dashboard (la riapro a fine caricamento)..."
  pkill -f radar_live.py
  sleep 1.5
fi

riapri_dashboard() {
  # RIAPRI=0 quando subito dopo vuoi leggere la seriale tu: altrimenti la
  # dashboard se la prende e il lettore trova la porta occupata.
  if [ "${RIAPRI:-1}" = "1" ] && [ "$DASHBOARD_ERA_ATTIVA" = "1" ]; then
    echo "riapro la dashboard su http://localhost:8080"
    ( "$(dirname "$0")/radar-live.sh" >/tmp/radarlive.log 2>&1 & )
  fi
}
trap riapri_dashboard EXIT

# Cerco la scheda su tutti i nomi possibili: USB nativa dell'S3
# (usbmodem) oppure un ponte seriale CH340/CP2102 (wchusbserial/usbserial/SLAB).
trova_porta() {
  ls -1 /dev/cu.* 2>/dev/null \
    | grep -vi "bluetooth\|debug-console" \
    | grep -iE "usbmodem|usbserial|wchusbserial|SLAB" \
    | head -1
}

PORTA="${PORTA_OVERRIDE:-$(trova_porta || true)}"

# --wait: aspetto che compaia, cosi' puoi fare con calma BOOT+RESET
if [ -z "$PORTA" ] && [ "${ATTENDI:-0}" = "1" ]; then
  echo "aspetto la scheda (30s)... fai ora BOOT+RESET"
  for _ in $(seq 30); do
    sleep 1
    PORTA="${PORTA_OVERRIDE:-$(trova_porta || true)}"
    [ -n "$PORTA" ] && break
  done
fi

if [ -z "$PORTA" ]; then
  # Distinguo i due casi: scheda proprio assente, oppure scheda collegata ma
  # senza driver. Sono problemi diversi e le soluzioni non c'entrano nulla.
  VISTA_USB=$(ioreg -p IOUSB -w0 2>/dev/null | grep -ci "USB Serial" || true)
  CHROME=$(ioreg -w0 -l -r -n "USB Serial" 2>/dev/null | grep -ci "Google Chrome" || true)
  DRIVER=$(systemextensionsctl list 2>/dev/null | grep -ci "wch\|ch34" || true)

  echo "❌ Nessuna porta seriale disponibile."
  echo
  if [ "$VISTA_USB" != "0" ]; then
    echo "   ✅ La scheda E' collegata: il Mac la vede nell'albero USB."
    if [ "$CHROME" != "0" ]; then
      echo "   ❌ Ma CHROME se l'e' presa via WebUSB e la tiene in esclusiva."
      echo "      -> chiudi Chrome con Cmd-Q, stacca e riattacca la scheda."
    elif [ "$DRIVER" = "0" ]; then
      echo "   ❌ Manca il driver del ponte seriale CH340 (1A86:7522)."
      echo "      macOS ha AppleUSBCHCOM ma non aggancia questo PID."
      echo
      echo "      -> Mac App Store: installa \"CH34xVCPDriver\" (WCH, gratis)"
      echo "      -> aprilo una volta"
      echo "      -> Impostazioni > Privacy e Sicurezza: approva l'estensione"
      echo "      -> riavvia se lo chiede, poi rilancia questo comando"
    else
      echo "   ❓ Il driver risulta installato ma la porta non compare:"
      echo "      stacca e riattacca la scheda, o riavvia il Mac."
    fi
  else
    echo "   La scheda non compare proprio nell'albero USB:"
    echo "   1. cavo USB-C DATI (quello del caricabatterie spesso non porta dati)"
    echo "   2. collegata al Mac, non a un alimentatore o a un hub"
    echo "   3. BOOT+RESET, poi:  ATTENDI=1 ./scripts/flash.sh"
  fi
  echo
  echo "   Porte viste adesso:"
  ls -1 /dev/cu.* 2>/dev/null | sed "s/^/     /" || echo "     (nessuna)"
  exit 1
fi

echo "porta:   $PORTA"
echo "sketch:  $SKETCH"
echo
"$CLI" compile --fqbn "$FQBN" "$SKETCH"
echo
echo "carico..."
"$CLI" upload -p "$PORTA" --fqbn "$FQBN" "$SKETCH"
echo
echo "✅ caricato."

if [ "$MONITOR" = "1" ]; then
  echo "monitor seriale a 115200 — ctrl-C per uscire"
  sleep 2
  "$CLI" monitor -p "$PORTA" -c baudrate=115200
else
  echo "per leggere:  ./scripts/flash.sh --monitor   (oppure il monitor della IDE a 115200)"
fi
