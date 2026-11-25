# Registro preliminar de riesgos

Complementa el [registro técnico existente](../hardware/A6-REGISTRO-RIESGOS.md). Prioridad propuesta, pendiente de aprobación.

| ID | Riesgo | Prioridad | Mitigación propuesta |
| --- | --- | --- | --- |
| R-01 | Pánico sin confirmación de entrega | Alta | Revisar contrato y probar pérdida de eventos |
| R-02 | Confusión entre cámara Arduino y ESP-IDF | Alta | Identificar build y fuente antes de actualizar |
| R-03 | microSD intermitente | Alta | Diagnóstico y ensayos de recuperación sin borrar |
| R-04 | Repositorio público con datos operativos | Alta | Auditoría privada, rotación si procede y decisión de visibilidad |
| R-05 | CAD incompatible con PCB | Media | Inspección de primera pieza y revisión vinculada |
| R-06 | Clones divergentes | Alta | Comparar commits y preservar cambios |
| R-07 | Manual incompatible con revisión instalada | Media | Vincular manual, hardware y firmware |

Todos abiertos; no se certifica ninguna mitigación sin evidencia.
