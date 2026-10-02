#!/usr/bin/env bash
# Carica sul Freenove ESP32 (WROOM-32E), che e' un ESP32 classico:
# FQBN diverso da quello del CrowPanel, che invece e' un S3.
#
# UploadSpeed=115200 e' obbligatorio: anche questa scheda ha un ponte CH340,
# e al default di 921600 il caricamento dello stub fallisce
# ("Invalid (unsupported) command").
#
#   ./scripts/flash-freenove.sh
#   PORTA_OVERRIDE=/dev/cu.usbserial-0001 ./scripts/flash-freenove.sh
set -e
cd "$(dirname "$0")/.."
# Specchio del controllo in flash.sh: qui si carica per ESP32 classico, e
# sulla porta puo' esserci la CrowPanel.
PORTA_C="${PORTA_OVERRIDE:-$(ls /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
ESPTOOL=$(find "$HOME/Library/Arduino15/packages/esp32" -name esptool -type f 2>/dev/null | head -1)
if [ -n "$ESPTOOL" ] && [ -n "$PORTA_C" ]; then
  CHIP=$("$ESPTOOL" --port "$PORTA_C" chip_id 2>/dev/null | sed -n 's/^Chip is \([A-Za-z0-9-]*\).*/\1/p' | head -1)
  case "$CHIP" in
    ESP32-S3)
      echo "Sulla porta c'e' una CrowPanel (ESP32-S3), non il Freenove."
      echo "Per il pannello usa:  ./scripts/flash.sh $*"
      exit 1 ;;
  esac
  [ -n "$CHIP" ] && echo "scheda:  $CHIP"
fi

export FQBN_OVERRIDE="esp32:esp32:esp32:UploadSpeed=${VELOCITA:-115200}"
exec ./scripts/flash.sh "${1:-firmware/radar_freenove}"
