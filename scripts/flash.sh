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

# Nessuno sketch predefinito: ce n'era uno, e un "--monitor" da solo finiva per
# caricare uno sketch vecchio sulla scheda sbagliata senza che nessuno l'avesse
# chiesto. Chi vuole caricare lo dice.
SKETCH=""
MONITOR=0
for arg in "$@"; do
  case "$arg" in
    --monitor) MONITOR=1 ;;
    *) SKETCH="$arg" ;;
  esac
done

if [ -z "$SKETCH" ] && [ "$MONITOR" = "0" ]; then
  echo "uso:  $0 <cartella-sketch> [--monitor]"
  echo "      $0 --monitor            (solo lettura, non carica niente)"
  exit 2
fi

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

# Le due schede si alternano sulla stessa porta e caricare sulla sbagliata e'
# gia' successo piu' volte. Chiedo al chip chi e' prima di scrivergli addosso.
ESPTOOL=$(find "$HOME/Library/Arduino15/packages/esp32" -name esptool -type f 2>/dev/null | head -1)
if [ -n "$ESPTOOL" ]; then
  CHIP=$("$ESPTOOL" --port "$PORTA" chip_id 2>/dev/null | sed -n 's/^Chip is \([A-Za-z0-9-]*\).*/\1/p' | head -1)
  case "$FQBN:$CHIP" in
    *esp32s3*:ESP32-S3) ;;
    *esp32s3*:ESP32*)
      echo "Sulla porta c'e' un $CHIP, ma questo script carica per ESP32-S3."
      echo "Per il Freenove usa:  ./scripts/flash-freenove.sh $SKETCH"
      exit 1 ;;
  esac
  [ -n "$CHIP" ] && echo "scheda:  $CHIP"
fi

if [ -z "$SKETCH" ]; then
  MONITOR=1
else
echo "sketch:  $SKETCH"
echo
"$CLI" compile --fqbn "$FQBN" "$SKETCH"
echo
echo "carico..."
"$CLI" upload -p "$PORTA" --fqbn "$FQBN" "$SKETCH"
echo
echo "✅ caricato."
fi

if [ "$MONITOR" = "1" ]; then
  echo "monitor seriale a 115200 — ctrl-C per uscire"
  sleep 2
  "$CLI" monitor -p "$PORTA" -c baudrate=115200
else
  echo "per leggere:  ./scripts/flash.sh --monitor   (oppure il monitor della IDE a 115200)"
fi
