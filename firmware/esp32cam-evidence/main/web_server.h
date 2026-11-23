#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicia servidor HTTP local en puerto 80 (solo diagnóstico)
esp_err_t web_server_start(void);

// Detiene servidor
void web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H