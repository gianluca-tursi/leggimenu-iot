#!/usr/bin/env bash
# Dashboard radar in tempo reale. Sottile involucro attorno a dashboard.sh,
# tenuto per non rompere le abitudini di chi lo lancia a memoria.
exec "$(dirname "$0")/dashboard.sh" avvia
