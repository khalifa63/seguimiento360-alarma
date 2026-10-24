#!/usr/bin/env bash
# Compila y ejecuta las pruebas de logica de firmware en el PC.
#
# La funcion bajo prueba se extrae de su archivo original en lugar de
# copiarla: si se copiara, la prueba podria seguir pasando con el codigo real
# ya cambiado.
set -u

AQUI="$(cd "$(dirname "$0")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"
FALLO=0

echo "== validador de claim codes =="
python3 "$AQUI/generar_test_claim.py" > "$AQUI/test_claim_code.c" || exit 1
if gcc -Wall -Wextra -o "$AQUI/test_claim_code" "$AQUI/test_claim_code.c"; then
    "$AQUI/test_claim_code" || FALLO=1
else
    echo "  no compila"
    FALLO=1
fi
rm -f "$AQUI/test_claim_code" "$AQUI/test_claim_code.c"

exit $FALLO
