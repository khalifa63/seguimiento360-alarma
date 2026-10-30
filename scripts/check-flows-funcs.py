#!/usr/bin/env python3
"""Guardia de sintaxis para los function nodes de un flow de Node-RED.

Un SyntaxError silencioso en un function node no rompe el deploy: Node-RED
no compila la funcion hasta que llega el primer mensaje, y el endpoint queda
mudo (el navegador se queda esperando la respuesta). Ya ocurrio con el
endpoint de salud del 2026-10-30 (ver
docs/operaciones/2026-10-30-integracion-buzzer.md, hallazgo 3).

Uso:  python3 scripts/check-flows-funcs.py <flows.json>
Sale 0 si todos los function nodes compilan; 1 e imprime las fallas.
Requiere node en el PATH.
"""
import json, subprocess, sys

path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/flows_new.json'
f = json.load(open(path))
fallas = []
total = 0
for n in f:
    if n.get('type') != 'function':
        continue
    total += 1
    src = 'async function _f(msg, node, context, global) {\n' + n.get('func', '') + '\n}'
    with open('/tmp/_fncheck.js', 'w') as fh:
        fh.write(src)
    r = subprocess.run(['node', '--check', '/tmp/_fncheck.js'],
                       capture_output=True, text=True)
    if r.returncode != 0:
        fallas.append((n['id'], n.get('name'), (r.stderr or '?').splitlines()[0]))
if fallas:
    for fid, name, err in fallas:
        print('FALLA', fid, repr(name), err, file=sys.stderr)
    sys.exit(1)
print(f'OK: sintaxis de {total} function nodes en {path}')
