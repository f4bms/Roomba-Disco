#!/bin/sh
# Push de la rama actual y sincronización bidireccional con development.
# Orden: 1) push de la rama actual tal cual está (para no mezclar merges con
# los commits propios antes de que queden en el remoto), 2) trae development
# a la rama actual, 3) lleva la rama actual a development (solo local:
# development no se pushea acá, eso lo hace Jeremy a mano).
# Correr desde dentro del repo (workarea/Roomba-Disco), parado en la rama a pushear.
# Uso: ./sync-push.sh

set -e

RAMA_BASE="development"
RAMA_ACTUAL=$(git rev-parse --abbrev-ref HEAD)

if [ "$RAMA_ACTUAL" = "main" ] || [ "$RAMA_ACTUAL" = "$RAMA_BASE" ]; then
    echo "Este script no se corre parado en $RAMA_ACTUAL; cambiá a tu rama de trabajo."
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "Hay cambios sin commitear:"
    git status --short
    printf "¿Continuar de todas formas? [y/N] "
    read -r RESPUESTA
    case "$RESPUESTA" in
        [yY]|[yY][eE][sS]) ;;
        *) echo "Cancelado."; exit 1 ;;
    esac
fi

echo "Pusheando $RAMA_ACTUAL a origin..."
git push -u origin "$RAMA_ACTUAL"

echo "Actualizando referencias remotas..."
git fetch origin

echo "Fusionando origin/$RAMA_BASE en $RAMA_ACTUAL..."
git merge "origin/$RAMA_BASE"

echo "Fusionando $RAMA_ACTUAL en $RAMA_BASE..."
git checkout "$RAMA_BASE"
git merge "$RAMA_ACTUAL"
git push -u origin "$RAMA_BASE"
git checkout "$RAMA_ACTUAL"

echo "$RAMA_ACTUAL pusheada. $RAMA_BASE actualizada."
