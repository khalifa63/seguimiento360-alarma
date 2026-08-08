# Documentación - Seguimiento 360 Alarmas

Actualizada: 2026-08-08

## Estructura

    docs/
      README.md                          Este archivo
      fases/RESUMEN_FASES.md             Estado y lecciones por fase
      infraestructura/FLUJOS_NODERED.md  API y flujos Node-RED completos
      patrones/PATRONES_DISENO.md        Soluciones probadas y anti-patrones

## Nota de metodología

Los bloques de código de esta documentación usan indentación de 4 espacios.
NUNCA se usan triple backticks: así cualquier bloque del repo puede copiarse
al terminal sin romper heredocs.

## Para sesiones nuevas (IA o humanas)

1. Leer fases/RESUMEN_FASES.md para entender el estado actual.
2. Revisar patrones/PATRONES_DISENO.md antes de escribir código Node-RED o PWA.
3. Consultar infraestructura/FLUJOS_NODERED.md para detalle de endpoints.
4. Infraestructura del servidor: infraestructura_servidor.pdf (raiz del repo).

## Checklist de calidad antes de cerrar fase

- [ ] Endpoints probados con curl (200/201/400/401/404)
- [ ] Aislamiento multi-tenant verificado con dos tenants distintos
- [ ] Frontend probado en navegador (Ctrl+Shift+R)
- [ ] Documentación actualizada (esta carpeta)
- [ ] Commit descriptivo (feat/fix/docs) + push
- [ ] Snapshot de flows.json copiado del contenedor al repo

## Comandos útiles

    docker logs nodered --tail 50
    docker exec postgres psql -U admin -d seguimiento360_alarmas -c "\dt"
    docker exec nodered grep -c "functionGlobalContext:" /data/settings.js
    ./scripts/deploy-pwa.sh

## Regla de oro frontend (agregada 2026-08-08)

Antes de entregar cualquier bloque que modifique frontend/pwa/:
1. Verificar exports reales: grep -n "export" frontend/pwa/js/*.js
2. Usar imports con ?v=__V__
3. Incluir ./scripts/deploy-pwa.sh antes del commit
4. Pedir prueba en incognito (descarta cache)
