#pragma once
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ota_manager_start(const char *firmware_url);

#ifdef __cplusplus
}
#endif
