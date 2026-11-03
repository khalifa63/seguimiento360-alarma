# Seguimiento360 Alarma — sistema oficial de identidad

## Decisión de marca

El sistema oficial se construye alrededor de **Conexión**, porque expresa modularidad: cada usuario puede comenzar con un módulo y ampliar su ecosistema de seguridad sin cambiar de marca.

- **Logo oficial:** `logo-opcion-03-conexion.svg`
- **Icono PWA:** `iconos-conexion/conexion-pwa-navy.svg`
- **Splash:** `iconos-conexion/conexion-splash.svg`
- Las variantes antiguas fueron retiradas del repositorio y no deben usarse.

## Concepto

El rombo abierto representa un sistema que conecta módulos. El nodo central simboliza la inteligencia de monitoreo; los nodos laterales representan dispositivos, personas y activos. La composición es estable, simple y escalable.

## Carta de colores

| Color | HEX | RGB | CMYK aprox. | Uso |
|---|---|---|---|---|
| Azul naval | `#1A3E72` | 26, 62, 114 | 77, 46, 0, 55 | Marca, encabezados, superficies principales |
| Ciber-azul | `#5DC1E1` | 93, 193, 225 | 59, 14, 0, 12 | Conexión, interacción, gráficos y estados informativos |
| Ámbar | `#FFD166` | 255, 209, 102 | 0, 18, 60, 0 | Atención, CTA y estados que requieren acción |
| Tinta | `#0A182C` | 10, 24, 44 | 77, 45, 0, 83 | Texto fuerte, fondos oscuros y splash |
| Niebla | `#F8F9FA` | 248, 249, 250 | 1, 0, 0, 2 | Fondo, respiración y superficies neutras |

**Proporción recomendada:** 45% Niebla, 25% Azul naval, 15% Tinta, 10% Ciber-azul, 5% Ámbar.

## Usos correctos

- Mantener proporciones y espacios del SVG.
- Usar Azul naval o Tinta en fondos oscuros y Niebla en fondos claros.
- Reservar Ámbar para acción/atención; no usarlo como color dominante.
- Mantener una zona libre alrededor del logo equivalente al diámetro del nodo central.
- Para tamaños menores de 24 px, usar solo el isotipo, sin texto.

## Usos incorrectos

No deformar, rotar, inclinar, aplicar sombras, añadir degradados, cambiar colores, encerrar en un escudo/candado, colocar sobre fondos fotográficos sin placa de contraste ni comunicar estados solo mediante color.

## Tipografía

- **Display:** Space Grotesk, pesos 600–700.
- **Texto/UI:** Inter, pesos 400–600.
- Ambas soportan caracteres latinos extendidos y cuentan con licencia OFL.

## Accesibilidad

- Texto claro sobre Tinta o Azul naval.
- Texto Azul naval sobre Niebla.
- Ciber-azul y Ámbar funcionan principalmente como acentos; acompañar estados con texto o icono.
- Mantener contraste mínimo WCAG AA: 4.5:1 para texto normal y 3:1 para texto grande.

## Aplicación por canal

- **PWA:** icono Navy; interfaz con Azul naval y Niebla.
- **Splash:** Tinta, isotipo centrado y firma blanca.
- **Uniforme:** isotipo al pecho y firma horizontal en espalda.
- **Vehículo:** logo horizontal grande sobre Niebla; banda Ciber-azul.
- **Papelería:** fondo Niebla, encabezado Azul naval, Ámbar solo en totales o alertas.

## Control de versiones

Versión 1.0 · Sistema Conexión · Noviembre 2026. Antes de registrar la marca, realizar búsqueda denominativa y figurativa en la SIC de Colombia y validar disponibilidad de dominio.

## Versiones para web

- `web/logo-web-horizontal.svg`: cabecera, navegación y footer sobre fondo oscuro o neutro.
- `web/logo-web-claro.svg`: cabecera sobre fondo claro.
- `web/logo-web-cuadrado.svg`: avatar, tarjeta social y aplicaciones cuadradas.
- `iconos-conexion/seguimiento360-pwa.ico`: favicon/shortcut icon multi-resolución (16, 32, 48, 64, 128 y 256 px).

### Implementación recomendada

```html
<link rel="icon" href="/imagen-corporativa/iconos-conexion/seguimiento360-pwa.ico">
<link rel="apple-touch-icon" href="/imagen-corporativa/iconos-conexion/conexion-pwa-navy.svg">
```

En web no alterar el logo horizontal, no aplicar filtros CSS y conservar un área libre mínima equivalente al diámetro del nodo central. Usar `logo-web-cuadrado.svg` cuando el espacio disponible sea menor que 180 px de ancho.
