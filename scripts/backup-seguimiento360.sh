#!/bin/bash
# Backup desarrollo + plataforma - estrategia 3-2-1
REPO=~/seguimiento360-alarma
LOG=~/backup-seguimiento360-alarma.log
KEEP=7
DATE=$(date +%Y%m%d-%H%M%S)

# Detectar ruta del disco externo (buscar la partición montada más grande)
DISCO_EXTERNO=$(lsblk -o MOUNTPOINT,SIZE -bn | grep -v "^/ " | sort -k2 -n | tail -1 | awk '{print $1}')
if [ -n "$DISCO_EXTERNO" ] && [ -d "$DISCO_EXTERNO" ]; then
    DEST=$DISCO_EXTERNO/backup-seguimiento360-alarma
    echo "[$DATE] Disco externo detectado: $DISCO_EXTERNO" >> $LOG
else
    DEST=~/backups/backup-seguimiento360-alarma
    echo "[$DATE] WARNING: Disco externo no detectado, usando ~/backups/" >> $LOG
fi

echo "[$DATE] === Iniciando backup en $DEST ===" >> $LOG
mkdir -p $DEST/volumes $DEST/working-copy $DEST/iot-alarma-config

# 1. Repositorio: commit de seguridad + bundle + copia de trabajo
cd $REPO || { echo "[$DATE] ERROR: no existe $REPO" >> $LOG; exit 1; }
if [ -n "$(git status --porcelain)" ]; then
    git add -A >> $LOG 2>&1
    git commit -m "chore: commit automatico de backup ($DATE)" >> $LOG 2>&1
fi
git bundle create $DEST/repo-$DATE.bundle --all >> $LOG 2>&1
rsync -a --delete --exclude=.git $REPO/ $DEST/working-copy/ >> $LOG 2>&1

# 2. Configuracion de la plataforma
if [ -d ~/iot-alarma ]; then
    rsync -a --delete ~/iot-alarma/ $DEST/iot-alarma-config/ >> $LOG 2>&1
fi

# 3. Dump completo de PostgreSQL
if docker ps --format '{{.Names}}' | grep -q '^postgres$'; then
    docker exec postgres pg_dumpall -U admin 2>>$LOG | gzip > $DEST/postgres-$DATE.sql.gz
fi

# 4. Volumenes Docker del proyecto
for vol in $(docker volume ls -q | grep '^iot-alarma'); do
    docker run --rm -v "$vol":/src -v "$DEST/volumes":/dst alpine \
        tar czf "/dst/$vol-$DATE.tar.gz" -C /src . 2>>$LOG
done

# 5. Rotacion
ls -1t $DEST/repo-*.bundle 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
ls -1t $DEST/postgres-*.sql.gz 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
for vol in $(docker volume ls -q | grep '^iot-alarma'); do
    ls -1t $DEST/volumes/$vol-*.tar.gz 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm --
done

echo "[$DATE] === Backup completado en $DEST ===" >> $LOG
ls -lh $DEST/ >> $LOG
