#!/usr/bin/env python3
"""Genera la prueba del validador de claim codes.

Extrae el bloque de validacion directamente de firmware/hub/main/main.c, de
modo que la prueba siempre ejercita el codigo que se compila, no una copia.
"""
import pathlib
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
MAIN = RAIZ / "firmware/hub/main/main.c"

src = MAIN.read_text(encoding="utf-8", errors="replace")
try:
    ini = src.index("static const char CLAIM_CHARSET[]")
    fin = src.index("static esp_err_t save_config_handler")
except ValueError:
    sys.exit("No se encontro el validador en main.c. Cambio de nombre o de sitio?")

bloque = src[ini:fin]

CABECERA = """#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

"""

CASOS = """
int main(void) {
    struct { const char *c; bool esperado; const char *nota; } casos[] = {
        /* Codigos reales emitidos por el backend: deben validar. */
        {"S3-AZJH86-M", true,  "real"},
        {"S3-PWDD6B-Z", true,  "real"},
        {"S3-FXGNJG-X", true,  "real"},
        {"S3-G9HV2A-A", true,  "real"},
        {"S3-QRBG97-N", true,  "real"},
        /* Mal escritos: deben rechazarse. */
        {"S3-AZJH86-A", false, "checksum incorrecto"},
        {"S3-AZJH8-M",  false, "longitud corta"},
        {"S3-AZJH8O-M", false, "letra O fuera del charset"},
        {"",            false, "vacio"},
        {"S3AZJH86M",   false, "sin guiones"},
        {"S3-AZJH86-MM",false, "longitud larga"},
    };
    int total = (int)(sizeof(casos)/sizeof(casos[0]));
    int fallos = 0;
    for (int i = 0; i < total; i++) {
        bool r = claim_code_valido(casos[i].c);
        if (r != casos[i].esperado) {
            fallos++;
            printf("  FALLO  %-14s esperado=%s obtenido=%s  (%s)\\n",
                   casos[i].c[0] ? casos[i].c : "(vacio)",
                   casos[i].esperado ? "valido" : "invalido",
                   r ? "valido" : "invalido", casos[i].nota);
        }
    }
    printf("  %d casos, %d fallos\\n", total, fallos);
    return fallos != 0;
}
"""

print(CABECERA + bloque + CASOS)
