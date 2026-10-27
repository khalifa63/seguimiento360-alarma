// Genera el icono PNG sin dependencias: dibuja a mano y escribe el PNG con zlib
const fs = require('fs');
const zlib = require('zlib');

function crc32(buf) {
  let c;
  const tabla = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    tabla[n] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    crc = tabla[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function trozo(tipo, datos) {
  const largo = Buffer.alloc(4);
  largo.writeUInt32BE(datos.length, 0);
  const cuerpo = Buffer.concat([Buffer.from(tipo, 'ascii'), datos]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(cuerpo), 0);
  return Buffer.concat([largo, cuerpo, crc]);
}

function escribirPNG(ancho, alto, pixel) {
  // pixel(x, y) -> [r, g, b]
  const crudo = Buffer.alloc((ancho * 3 + 1) * alto);
  let p = 0;
  for (let y = 0; y < alto; y++) {
    crudo[p++] = 0; // filtro none
    for (let x = 0; x < ancho; x++) {
      const [r, g, b] = pixel(x, y);
      crudo[p++] = r;
      crudo[p++] = g;
      crudo[p++] = b;
    }
  }

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(ancho, 0);
  ihdr.writeUInt32BE(alto, 4);
  ihdr[8] = 8;   // bits por canal
  ihdr[9] = 2;   // color type RGB
  ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    trozo('IHDR', ihdr),
    trozo('IDAT', zlib.deflateSync(crudo, { level: 9 })),
    trozo('IEND', Buffer.alloc(0)),
  ]);
}

// --- Dibujo ---
const AZUL   = [79, 124, 255];
const FONDO  = [15, 20, 32];
const BLANCO = [232, 236, 244];

function crear(tam) {
  const cx = tam / 2;
  const cy = tam / 2;
  const lado = tam * 0.68;          // zona del escudo
  const anchoE = lado * 0.78;
  const altoE  = lado * 0.92;
  const gx0 = cx - anchoE / 2, gx1 = cx + anchoE / 2;
  const gy0 = cy - altoE / 2,  gy1 = cy + altoE / 2;
  const codo = gy0 + altoE * 0.55;  // donde empieza la punta

  // Puntos del check (relativos al escudo)
  const a = [cx - anchoE * 0.18, cy + altoE * 0.02];
  const b = [cx - anchoE * 0.04, cy + altoE * 0.16];
  const c = [cx + anchoE * 0.22, cy - altoE * 0.14];
  const grosor = Math.max(1.2, tam * 0.028) / 2;

  function distSegmento(px, py, p1, p2) {
    const dx = p2[0] - p1[0], dy = p2[1] - p1[1];
    const l2 = dx * dx + dy * dy || 1;
    let t = ((px - p1[0]) * dx + (py - p1[1]) * dy) / l2;
    t = Math.max(0, Math.min(1, t));
    const qx = p1[0] + t * dx, qy = p1[1] + t * dy;
    return Math.hypot(px - qx, py - qy);
  }

  return escribirPNG(tam, tam, (x, y) => {
    const px = x + 0.5, py = y + 0.5;

    // ¿Está dentro del escudo?
    if (px < gx0 || px > gx1 || py < gy0 || py > gy1) return FONDO;

    let dentro = false;
    if (py <= codo) {
      dentro = true;                       // rectángulo superior
    } else {
      // triángulo inferior: ancho se reduce hasta 0 en gy1
      const t = (py - codo) / (gy1 - codo);
      const media = (anchoE / 2) * (1 - t);
      dentro = Math.abs(px - cx) <= media;
    }
    if (!dentro) return FONDO;

    // ¿Es parte del check?
    const d1 = distSegmento(px, py, a, b);
    const d2 = distSegmento(px, py, b, c);
    if (d1 <= grosor || d2 <= grosor) return FONDO;

    return AZUL;
  });
}

const destino = '/home/antonio/iot-alarma/website/portal/';
for (const tam of [192, 512]) {
  const png = crear(tam);
  fs.writeFileSync(destino + 'icono-' + tam + '.png', png);
  console.log('creado: icono-' + tam + '.png (' + png.length + ' bytes)');
}
fs.writeFileSync(destino + 'icono.png', crear(192));
console.log('creado: icono.png (192)');
console.log('listo');
