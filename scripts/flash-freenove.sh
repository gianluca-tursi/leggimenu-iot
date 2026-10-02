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
export FQBN_OVERRIDE="esp32:esp32:esp32:UploadSpeed=${VELOCITA:-115200}"
exec ./scripts/flash.sh "${1:-firmware/radar_freenove}"
