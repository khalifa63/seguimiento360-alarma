from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.enum.style import WD_STYLE_TYPE

OUT='Manual_Identidad_Seguimiento360_Alarma.docx'
doc=Document()
sec=doc.sections[0]
sec.top_margin=Inches(.6); sec.bottom_margin=Inches(.6); sec.left_margin=Inches(.7); sec.right_margin=Inches(.7)
# palette
navy='0B1F3A'; cyan='00B8D9'; orange='FF7A00'; light='F2F6FA'; gray='425466'; white='FFFFFF'
def shade(cell, fill):
    tcPr=cell._tc.get_or_add_tcPr(); shd=OxmlElement('w:shd'); shd.set(qn('w:fill'),fill); tcPr.append(shd)
def set_cell_text(cell, text, color=navy, bold=False, size=9):
    cell.text=''; p=cell.paragraphs[0]; p.alignment=WD_ALIGN_PARAGRAPH.LEFT
    r=p.add_run(text); r.font.name='Aptos'; r.font.size=Pt(size); r.bold=bold; r.font.color.rgb=RGBColor.from_string(color)
    cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER

def add_rule(color=cyan):
    p=doc.add_paragraph(); p.paragraph_format.space_after=Pt(5)
    r=p.add_run('━'*72); r.font.color.rgb=RGBColor.from_string(color); r.font.size=Pt(5)

def heading(text, level=1):
    p=doc.add_paragraph(); p.paragraph_format.space_before=Pt(10); p.paragraph_format.space_after=Pt(4)
    r=p.add_run(text); r.bold=True; r.font.name='Aptos Display'; r.font.size=Pt(18 if level==1 else 12); r.font.color.rgb=RGBColor.from_string(navy)
    return p

def body(text, boldlead=None):
    p=doc.add_paragraph(); p.paragraph_format.space_after=Pt(5); p.paragraph_format.line_spacing=1.08
    if boldlead and text.startswith(boldlead):
        r=p.add_run(boldlead); r.bold=True; r.font.color.rgb=RGBColor.from_string(navy); r.font.name='Aptos'; r.font.size=Pt(9.5)
        r=p.add_run(text[len(boldlead):]); r.font.name='Aptos'; r.font.size=Pt(9.5); r.font.color.rgb=RGBColor.from_string(gray)
    else:
        r=p.add_run(text); r.font.name='Aptos'; r.font.size=Pt(9.5); r.font.color.rgb=RGBColor.from_string(gray)
    return p

def bullets(items):
    for x in items:
        p=doc.add_paragraph(style='List Bullet'); p.paragraph_format.space_after=Pt(2)
        r=p.add_run(x); r.font.name='Aptos'; r.font.size=Pt(9); r.font.color.rgb=RGBColor.from_string(gray)

# styles
for s in ['Normal','List Bullet']:
    st=doc.styles[s]; st.font.name='Aptos'; st.font.size=Pt(9.5); st.font.color.rgb=RGBColor.from_string(gray)
# cover
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER; p.paragraph_format.space_before=Pt(55)
r=p.add_run('SEGUIMIENTO'); r.bold=True; r.font.name='Aptos Display'; r.font.size=Pt(34); r.font.color.rgb=RGBColor.from_string(navy)
r=p.add_run('360'); r.bold=True; r.font.name='Aptos Display'; r.font.size=Pt(34); r.font.color.rgb=RGBColor.from_string(cyan)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('ALARMA'); r.bold=True; r.font.name='Aptos Display'; r.font.size=Pt(17); r.font.color.rgb=RGBColor.from_string(orange)
add_rule(); p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('Manual de identidad de marca'); r.font.name='Aptos'; r.font.size=Pt(18); r.font.color.rgb=RGBColor.from_string(navy)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('Versión 1.0 · Noviembre 2026'); r.font.name='Aptos'; r.font.size=Pt(10); r.font.color.rgb=RGBColor.from_string(gray)
for _ in range(5): doc.add_paragraph('')
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('Seguridad que se siente. Control que se demuestra.'); r.italic=True; r.font.name='Aptos'; r.font.size=Pt(12); r.font.color.rgb=RGBColor.from_string(cyan)
doc.add_page_break()
# intro
heading('01 · Esencia y estrategia')
body('Seguimiento360 Alarma convierte la seguridad en una experiencia clara, humana y accionable. Acompañamos a hogares, comercios y equipos para que sepan qué ocurre, reciban respuesta oportuna y mantengan el control.')
heading('Propósito',2); body('Proteger lo que importa mediante monitoreo inteligente, respuesta confiable y comunicación sin fricciones.')
heading('Promesa',2); body('Siempre cerca, siempre atentos, siempre en control.')
heading('Personalidad',2); body('Vigilante · cercana · precisa · resolutiva · transparente.')
heading('Mensajes clave',2); bullets(['Prevención antes que reacción.','Información clara en el momento correcto.','Tecnología al servicio de la tranquilidad.','Respaldo humano detrás de cada alerta.'])
heading('Sistema verbal',2); body('Tono: directo, sereno y empático. Evitar alarmismo, tecnicismos innecesarios y promesas absolutas. Preferir verbos de acción: monitoreamos, verificamos, avisamos, respondemos.')
doc.add_page_break()
# logo and colors
heading('02 · Identidad visual')
heading('Construcción del logotipo',2); body('El identificador combina el nombre Seguimiento360 con la categoría Alarma. “Seguimiento” comunica acompañamiento continuo; “360” expresa cobertura integral; el naranja activa la respuesta y el cian transmite tecnología y confianza.')
heading('Zona de protección',2); body('Mantener alrededor del logotipo un área libre equivalente a la altura de la letra “A” de ALARMA. Ningún texto, imagen, borde o elemento debe invadir esta zona. En formatos reducidos, conservar al menos 12 px de aire por lado.')
heading('Tamaños mínimos',2); bullets(['Digital: 120 px de ancho para versión completa; 32 px para el símbolo.','Impreso: 30 mm de ancho para versión completa; 10 mm para el símbolo.','Nunca deformar, rotar, añadir sombras ni cambiar el orden de los elementos.'])
heading('Paleta cromática',2)
t=doc.add_table(rows=1, cols=5); t.alignment=WD_TABLE_ALIGNMENT.CENTER; t.style='Table Grid'
for i,h in enumerate(['Color','HEX','RGB','CMYK aprox.','Pantone cercano']): set_cell_text(t.rows[0].cells[i],h,white,True,8); shade(t.rows[0].cells[i],navy)
rows=[('Azul profundo','#0B1F3A','11, 31, 58','81, 47, 0, 77','Pantone 2965 C'),('Cian alerta','#00B8D9','0, 184, 217','100, 15, 0, 15','Pantone 3125 C'),('Naranja respuesta','#FF7A00','255, 122, 0','0, 52, 100, 0','Pantone 1585 C'),('Niebla','#F2F6FA','242, 246, 250','3, 2, 0, 2','Pantone 656 C'),('Grafito','#425466','66, 84, 102','35, 18, 0, 60','Pantone 431 C')]
for name,he,rg,cm,pa in rows:
    cells=t.add_row().cells
    for i,txt in enumerate([name,he,rg,cm,pa]): set_cell_text(cells[i],txt,gray,False,8)
    shade(cells[0],he[1:])
heading('Tipografías open-source',2); body('Titulares: Plus Jakarta Sans (Google Fonts), semibold/bold. Texto corrido: Source Sans 3 (Google Fonts), regular/semibold. Ambas ofrecen soporte latino, excelente legibilidad y licencias abiertas. Fallback: Arial, sans-serif.')
doc.add_page_break()
# accessibility
heading('03 · Accesibilidad y producción')
heading('Contraste WCAG 2.2 AA',2)
body('Usar Azul profundo sobre Niebla o blanco para texto normal. El contraste aproximado Azul profundo/blanco es 16:1; Grafito/blanco, 7:1. El Cian se reserva para superficies oscuras o elementos grandes; no usar Cian como texto pequeño sobre blanco. El Naranja funciona como acento y llamada de acción sobre Azul profundo, no como texto fino sobre blanco.')
heading('Reglas de accesibilidad',2); bullets(['Texto normal mínimo 4,5:1; texto grande mínimo 3:1.','No comunicar estados solo con color: acompañar alertas con icono y texto.','Cuerpo digital mínimo 16 px; interlineado de 1,5 en lectura prolongada.','Foco de teclado visible y objetivos táctiles de al menos 44 × 44 px.','Añadir texto alternativo a logotipo e imágenes; respetar orden semántico de encabezados.'])
heading('Usos correctos e incorrectos',2); bullets(['Correcto: versión positiva en fondo claro y versión negativa en Azul profundo.','Correcto: respetar proporciones, colores y zona de protección.','Incorrecto: estirar, inclinar, contornear, aplicar degradados no autorizados o colocar sobre fondos de bajo contraste.','Incorrecto: sustituir la tipografía, separar “360” del nombre o usar el símbolo como señal de emergencia sin contexto.'])
heading('Iconografía y fotografía',2); body('Iconos lineales, esquinas suaves y trazo uniforme. Fotografías reales, luminosas y diversas: personas tranquilas, hogares habitados, comercios activos y equipos atentos. Evitar imágenes de pánico, intrusión explícita o estética militarizada.')
doc.add_page_break()
# applications
heading('04 · Aplicaciones')
heading('Canales digitales',2); bullets(['App y portal: Azul profundo como base, Cian para navegación activa y Naranja para acciones de respuesta.','Redes sociales: fondos de Niebla con bloques de Azul profundo y titulares en Plus Jakarta Sans.','Notificaciones: asunto informativo, estado visible, acción primaria única y datos de contacto verificables.'])
heading('Señalización y uniformes',2); body('En señalización, priorizar alto contraste y lectura a distancia. El logotipo puede ir en blanco sobre Azul profundo o en Azul profundo sobre blanco. En uniformes, bordado monocromo blanco o Azul profundo; reservar el Naranja para detalles de alta visibilidad.')
heading('Papelería y vehículos',2); body('Tarjeta: logotipo al frente, datos de contacto y QR al reverso. Vehículos: gran bloque Azul profundo, franja Cian y acento Naranja; mantener el logotipo legible a 10 m y no cubrir placas, luces ni elementos reglamentarios.')
heading('Ejemplo de llamada a la acción',2)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('¿Todo bajo control?'); r.bold=True; r.font.name='Aptos Display'; r.font.size=Pt(20); r.font.color.rgb=RGBColor.from_string(navy)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('Monitorea. Verifica. Responde.'); r.font.name='Aptos'; r.font.size=Pt(12); r.font.color.rgb=RGBColor.from_string(cyan)
heading('Gobernanza de marca',2); body('Toda pieza nueva debe validar: legibilidad, contraste, consistencia del logotipo, tono sereno y claridad de la acción. Mantener una biblioteca maestra de logotipos en SVG, PDF y PNG, con versiones positiva, negativa y monocroma.')
# footer
for section in doc.sections:
    footer=section.footer.paragraphs[0]; footer.alignment=WD_ALIGN_PARAGRAPH.CENTER
    r=footer.add_run('SEGUIMIENTO360 ALARMA  ·  MANUAL DE IDENTIDAD  ·  1.0'); r.font.name='Aptos'; r.font.size=Pt(8); r.font.color.rgb=RGBColor.from_string(gray)
doc.core_properties.title='Manual de identidad · Seguimiento360 Alarma'
doc.core_properties.subject='Guía estratégica y visual de marca'
doc.save(OUT)
print(OUT)
