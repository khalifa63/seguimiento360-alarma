/*
 * pairing.c — Implementacion de la vinculacion del nodo (FW-008).
 *
 * Usa la particion NVS por defecto, namespace "s360node".
 */
#include "pairing.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "pairing";

#define NVS_NS_NODE     "s360node"
#define NVS_KEY_HUBMAC  "hub_mac"

static uint8_t s_hub_mac[6];
static bool    s_linked = false;

esp_err_t pairing_load_hub_mac(uint8_t out_mac[6])
{
    if (out_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_NODE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* Namespace inexistente = nodo nunca vinculado. No es un error. */
        s_linked = false;
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = sizeof(s_hub_mac);
    err = nvs_get_blob(h, NVS_KEY_HUBMAC, s_hub_mac, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(s_hub_mac)) {
        s_linked = false;
        return ESP_ERR_NOT_FOUND;
    }

    /* Una MAC de ceros no es valida: se trata como no vinculado. */
    uint8_t acc = 0;
    for (int i = 0; i < 6; i++) {
        acc |= s_hub_mac[i];
    }
    if (acc == 0) {
        s_linked = false;
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(out_mac, s_hub_mac, sizeof(s_hub_mac));
    s_linked = true;
    return ESP_OK;
}

esp_err_t pairing_save_hub_mac(const uint8_t mac[6])
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Si ya es la misma, no se escribe: cada escritura desgasta la flash y
     * el ACK llega en cada evento. */
    if (s_linked && memcmp(s_hub_mac, mac, sizeof(s_hub_mac)) == 0) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_NODE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(h, NVS_KEY_HUBMAC, mac, sizeof(s_hub_mac));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        memcpy(s_hub_mac, mac, sizeof(s_hub_mac));
        s_linked = true;
        ESP_LOGI(TAG, "Hub guardado en NVS: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE(TAG, "No se pudo guardar el hub: %s", esp_err_to_name(err));
    }
    return err;
}

bool pairing_is_linked(void)
{
    return s_linked;
}

bool pairing_mac_changed(const uint8_t mac[6])
{
    if (mac == NULL) {
        return false;
    }
    if (!s_linked) {
        return true;
    }
    return memcmp(s_hub_mac, mac, sizeof(s_hub_mac)) != 0;
}
