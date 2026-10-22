#!/bin/bash
# ==========================================================
#  EXPORTA EL MARCO TEORICO A PDF (o HTML)
#  Requisitos: sudo apt install pandoc texlive-xetex texlive-lang-spanish
# ==========================================================
cd ~/seguimiento360-alarma/docs/marco-teorico || exit 1

cat portada.md 00_indice.md 01_introduccion.md 02_estado_del_arte.md \
    03_analisis_mercado.md 04_arquitectura_sistema.md 05_diseno_hardware.md \
    06_diseno_software.md 07_seguridad.md 08_despliegue_operacion.md \
    09_plan_negocio.md 10_conclusiones.md referencias.md \
    > /tmp/marco_completo.md

# Opcion A: PDF con LaTeX (requiere texlive-xetex, ~1 GB)
if command -v xelatex >/dev/null 2>&1; then
    pandoc /tmp/marco_completo.md -o ~/marco_teorico_seguimiento360.pdf \
        --pdf-engine=xelatex -V lang:es -V geometry:margin=2.5cm \
        -V fontsize=11pt -V documentclass:report
    echo "PDF generado: ~/marco_teorico_seguimiento360.pdf"
else
    # Opcion B: HTML independiente (imprimir a PDF desde el navegador)
    pandoc /tmp/marco_completo.md -o ~/marco_teorico_seguimiento360.html --standalone
    echo "HTML generado: ~/marco_teorico_seguimiento360.html"
    echo "Abrelo en el navegador y usa Imprimir > Guardar como PDF"
fi
