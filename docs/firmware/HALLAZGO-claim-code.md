# El claim code no cierra de punta a punta

Estado del flujo de vinculación de un hub con su cuenta. Documenta una brecha
encontrada al revisar HUB-F-016, no una funcionalidad terminada.

## Qué existe y funciona

El **backend está completo**. No hay que construirlo:

- `POST /api/admin/generate-codes` genera códigos.
- `POST /api/claim` los canjea: valida el JWT del usuario, comprueba el código
  contra la tabla `claim_codes` y crea el hub.
- La tabla `claim_codes` tiene `code`, `device_type`, `status`, `tenant_id`,
  `site_id`, `claimed_device_id` y `expires_at`.

Hay códigos emitidos. Al revisar la base había uno activo (`S3-AZJH86-M`,
tenant 2, tipo `alarm_hub`) y cuatro revocados.

## La brecha

Cada parte usa una **convención de número de serie distinta**, y ninguna
coincide con las otras:

| Quién | Formato del serial | Dónde |
|---|---|---|
| `/api/claim` | `SN-` + código, p.ej. `SN-S3-AZJH86-M` | nodo "Preparar UPSERT" |
| `/api/hubs` | `HUB-` + marca de tiempo | nodo "Prep INSERT hub" |
| Hub físico | su **MAC**, p.ej. `3cdc7562c2b0` | `HUB_ONLINE` |

El hub registrado hoy en la base tiene `serial_number = 3cdc7562c2b0`, que es
su MAC. Si alguien canjea un código, el backend crea un hub con serial
`SN-...`, y **ese registro nunca se corresponderá con el equipo físico**: el
dispositivo sigue reportando su MAC.

El propio backend lo deja entrever. El nodo que procesa el `hello` del hub
lleva este comentario:

    // Actualizar hub por ID (sin validar serial_number en este paso)
    // El serial_number ya fue validado cuando se registró el hub

Es decir: la vinculación se apoya en el ID del hub, no en el serial. Funciona
mientras el hub ya exista en la base, pero no cubre el caso que el claim code
existe para resolver — dar de alta un equipo nuevo sin configurarlo a mano.

## Por qué importa

El objetivo del claim code es desplegar hubs en serie: se conecta el equipo,
se introduce un código en la aplicación y queda vinculado a la cuenta. Con las
convenciones actuales ese flujo no cierra.

Además, hoy el portal del hub **pide `tenant_id` y `site_id` a mano**, que son
identificadores internos de la base. Un instalador no puede conocerlos.

## Qué haría falta

Un **serial único y estable** que las tres partes compartan. El candidato
natural es la MAC del hub: ya es única, ya está grabada, y el firmware ya la
reporta. Las opciones, de menos a más cambio:

1. **Alinear el backend con la MAC.** `/api/claim` crea el hub con el serial
   que el propio equipo reporta. Requiere que el código canjeado se asocie a
   un equipo concreto, no a uno genérico: hoy un código crea un hub nuevo.
2. **Añadir un serial en el firmware.** El hub lee un número de serie de NVS y
   lo publica en `HUB_ONLINE` y en `hello`. Es el cambio de contrato más
   honesto, pero exige un paso de fabricación que grabe ese número.
3. **Usar el claim code como identificador.** El hub guarda el código que le
   dieron y lo reporta; el backend lo usa para casar el equipo. Simple, pero
   ata la identidad del equipo a un código de un solo uso.

La decisión es de contrato entre firmware y backend, así que necesita change
request sobre SPEC-02 antes de tocar código.

## Lo que sí se puede hacer mientras tanto

- **No pedir `tenant_id` a mano.** Un claim code puede llevar el tenant
  asociado (la tabla ya lo tiene), así que el portal podría pedir solo el
  código y resolver el resto en el backend.
- **Publicar el claim code en `HUB_ONLINE`**, para que la aplicación sepa qué
  equipo está esperando ser reclamado.

Ambas cosas son cambios de firmware sin tocar el modelo de datos.
