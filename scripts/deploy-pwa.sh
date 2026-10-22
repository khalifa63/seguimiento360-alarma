#!/bin/bash
set -e
SRC=~/seguimiento360-alarma/frontend/pwa
DST=~/iot-alarma/website/app
V=$(date +%Y%m%d%H%M)
rm -rf $DST
cp -r $SRC $DST
grep -rl '__V__' $DST | xargs sed -i "s/__V__/$V/g"
echo "✅ PWA desplegada en $DST (versión $V)"
