#!/bin/bash
REPO=~/seguimiento360-alarma
LOG=~/backup-seguimiento360-alarma.log
KEEP=7
DATE=$(date +%Y%m%d-%H%M%S)

if [ -d /mnt/disco-externo ] && mountpoint -q /mnt/disco-externo; then
    DEST=/mnt/disco-externo/backup-seguimiento360-alarma
else
    DEST=~/backups/backup-seguimiento360-alarma
    echo "[$DATE] WARNING: disco externo no montado, usando ~/backups/" >> $LOG
fi

echo "[$DATE] === Backup en $DEST ===" >> $LOG
mkdir -p "$DEST/volumes" "$DEST/working-copy" "$DEST/iot-alarma-config"
cd "$REPO" || { echo "[$DATE] ERROR repo" >> $LOG; exit 1; }

if [ -n "$(git status --porcelain)" ]; then
    git add -A >> $LOG 2>&1
    git commit -m "chore: backup auto ($DATE)" >> $LOG 2>&1
fi

git bundle create "$DEST/repo-$DATE.bundle" --all >> $LOG 2>&1
rsync -a --delete --exclude=.git "$REPO/" "$DEST/working-copy/" >> $LOG 2>&1

if [ -d ~/iot-alarma ]; then
    rsync -a --delete --exclude=.env --exclude=postgres/ --exclude=mosquitto/config/passwd --exclude=mosquitto/config/acl ~/iot-alarma/ "$DEST/iot-alarma-config/" >> $LOG 2>&1
    [ -f ~/iot-alarma/.env ] && cp ~/iot-alarma/.env "$DEST/iot-alarma-config/.env-$DATE"
    if docker ps --format '{{.Names}}' | grep -q '^mosquitto$'; then
        mkdir -p "$DEST/iot-alarma-config/mosquitto/config"
        docker cp mosquitto:/mosquitto/config/. "$DEST/iot-alarma-config/mosquitto/config/" 2>>$LOG
    fi
fi

if docker ps --format '{{.Names}}' | grep -q '^postgres$'; then
    docker exec postgres pg_dumpall -U admin 2>>$LOG | gzip > "$DEST/postgres-$DATE.sql.gz"
fi

for vol in $(docker volume ls -q | grep '^iot-alarma'); do
    docker run --rm -v "$vol":/src -v "$DEST/volumes":/dst alpine \
        tar czf "/dst/$vol-$DATE.tar.gz" -C /src . 2>>$LOG
done

ls -1t "$DEST"/repo-*.bundle 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
ls -1t "$DEST"/postgres-*.sql.gz 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
for vol in $(docker volume ls -q | grep '^iot-alarma'); do
    ls -1t "$DEST/volumes/$vol"-*.tar.gz 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
done

echo "[$DATE] === OK ===" >> $LOG
echo "--- Contenido ---" >> $LOG
ls -lh "$DEST/" >> $LOG
