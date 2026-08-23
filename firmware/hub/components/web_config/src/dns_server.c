/*
 * dns_server.c
 * Servidor DNS minimalista para portal cautivo.
 * [AT-05] Responde cualquier consulta A con 192.168.4.1 (IP del SoftAP).
 */
#include "web_config.h"

#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "dns_srv";
static int s_sock = -1;
static TaskHandle_t s_task = NULL;

#define DNS_PORT 53
#define DNS_BUF  512

/* Cabecera DNS mínima */
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_task(void *arg)
{
    uint8_t buf[DNS_BUF];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    while (1) {
        int n = recvfrom(s_sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
        if (n < 12) continue;

        dns_header_t *hdr = (dns_header_t *)buf;
        /* Solo consultas (QR=0) con QDCOUNT=1 */
        if ((hdr->flags & htons(0x8000)) != 0 || hdr->qdcount == 0) continue;

        /* Localizar fin del nombre de la query */
        int pos = 12;
        while (pos < n && buf[pos] != 0) {
            pos += buf[pos] + 1;
        }
        pos++;  /* salto el 0 final del nombre */
        if (pos + 4 > n) continue;
        uint16_t qtype = (buf[pos] << 8) | buf[pos + 1];
        pos += 4;

        /* Construir respuesta */
        uint8_t resp[DNS_BUF];
        int r = 0;
        resp[r++] = buf[0]; resp[r++] = buf[1];   /* ID */
        resp[r++] = 0x81; resp[r++] = 0x80;       /* QR=1, AA=1, RCODE=0 */
        resp[r++] = 0x00; resp[r++] = 0x01;       /* QDCOUNT=1 */
        resp[r++] = 0x00; resp[r++] = (qtype == 1) ? 0x01 : 0x00; /* ANCOUNT */
        resp[r++] = 0x00; resp[r++] = 0x00;
        resp[r++] = 0x00; resp[r++] = 0x00;

        /* Copiar sección de pregunta tal cual */
        int qlen = pos - 12;
        memcpy(resp + r, buf + 12, qlen);
        r += qlen;

        /* Solo responder consultas tipo A (1) con 192.168.4.1 */
        if (qtype == 1) {
            resp[r++] = 0xC0; resp[r++] = 0x0C;   /* pointer al nombre */
            resp[r++] = 0x00; resp[r++] = 0x01;   /* TYPE A */
            resp[r++] = 0x00; resp[r++] = 0x01;   /* CLASS IN */
            resp[r++] = 0x00; resp[r++] = 0x00;
            resp[r++] = 0x00; resp[r++] = 0x3C;   /* TTL 60s */
            resp[r++] = 0x00; resp[r++] = 0x04;   /* RDLENGTH 4 */
            resp[r++] = 192; resp[r++] = 168;
            resp[r++] = 4;   resp[r++] = 1;       /* 192.168.4.1 */
        }

        sendto(s_sock, resp, r, 0, (struct sockaddr *)&from, fromlen);
    }
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return ESP_FAIL;
    }
    int reuse = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed");
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    xTaskCreatePinnedToCore(dns_task, "dns_task", 3072, NULL, 5, &s_task, 0);
    ESP_LOGI(TAG, "DNS cautivo escuchando en :53");
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
