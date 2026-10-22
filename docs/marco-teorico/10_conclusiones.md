# CAPÍTULO 10: CONCLUSIONES Y TRABAJO FUTURO

## 10.1 Conclusiones

Primera. El objetivo general fue alcanzado en su etapa de diseño: se definio una infraestructura integral de alarma y monitoreo para hogar y vehiculos sobre un modelo de cuatro capas (percepcion, borde, nube y aplicacion), tecnicamente viable con tecnologia madura y economicamente sostenible al apoyarse en la infraestructura de servidor ya operativa de la plataforma Seguimiento 360.

Segunda. El modelo multi-inquilino con jerarquia inquilino-sitio-hub-dispositivo, impuesto mediante ACL en el broker y filtrado por token en el backend, garantiza el aislamiento de datos requerido para escalar a multiples clientes sin redisenar la arquitectura.

Tercera. La decision de dotar al hub ESP32-S3 de logica autonoma (maquina de estados finitos, store-and-forward y sirena local) cumple el requisito de que la proteccion del sitio no dependa de la disponibilidad de internet, diferenciando al sistema frente a soluciones de bajo costo totalmente dependientes de la nube.

Cuarta. El enlace ESP-NOW cifrado por inquilino para los nodos sensores, combinado con sueno profundo y despertar por interrupcion, permite autonomias de seis a veinticuatro meses con baterias pequenas, validando la viabilidad del hardware de bajo costo seleccionado.

Quinta. El rastreador vehicular basado en ESP32-S3 y A7670SA-FASE, con protecciones de grado automotriz y bateria de respaldo con deteccion de sabotaje, integra el segmento vehicular a la misma plataforma del hogar, habilitando reglas cruzadas que ningun competidor directo ofrece de forma nativa.

Sexta. La estrategia de seguridad por defensa en profundidad, junto al plan de endurecimiento que elimina accesos anonimos y credenciales por defecto documentados en la infraestructura actual, establece las condiciones para un lanzamiento comercial responsable.

Septima. La estrategia de continuidad basada en la regla 3-2-1 para el desarrollo y en respaldos diarios con RPO y RTO cuantificados para el servidor convierte la operacion en un componente del producto y no en una improvisacion posterior.

Octava. El analisis de mercado confirmo que el posicionamiento correcto no es competir por precio con fabricantes asiaticos ni por marca con actores globales, sino ocupar el espacio intermedio de hardware asequible sin suscripcion obligatoria, servidor local y soporte regional, con un plan de negocio que muestra utilidad positiva desde el segundo trimestre aun en el escenario conservador.

## 10.2 Lecciones Aprendidas

Del proceso de definicion se desprenden lecciones que guiaran la ejecucion: definir alcance y limites antes de codificar reduce reproceso; el analisis competitivo temprano evita posicionamientos inviables; la seguridad y la continuidad deben disenarse desde el inicio y no agregarse al final; y el principio de minima infraestructura viable (posponer camaras, OpenCPU, CI/CD local y certificaciones) concentra el esfuerzo en el producto esencial. Asimismo, documentar formalmente cada decision, como se hace en este marco teorico, transforma el conocimiento tacito en un activo transferible del negocio.

## 10.3 Trabajo Futuro

Corto plazo (Fase 2 tecnica): integracion de camaras IP y ESP32-CAM con ONVIF; habilitacion de Secure Boot y cifrado de flash en los dispositivos; listener MQTT TLS 8883 extremo a extremo; evaluacion de CBOR o Protobuf para payloads; y migracion del hardware de prototipado a PCB propia.

Mediano plazo (Ano 2): modulo white-label para instaladores; servicios premium con facturacion recurrente; integraciones con centrales de monitoreo externas y canales SMS y llamada; analisis predictivo con inteligencia artificial apoyado en las herramientas de desarrollo ya seleccionadas; y downsampling de series temporales para controlar el crecimiento de datos.

Largo plazo: expansion a escenarios rurales con LoRa; evaluacion de OpenCPU en el rastreador para optimizacion de costo; plan de certificaciones por mercado; alta disponibilidad del broker y del servidor; e internacionalizacion a otros paises de la region replicando el modelo de servidor local y soporte regional.
