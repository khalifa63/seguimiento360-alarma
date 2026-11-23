#include "sd_card.h"
#include "camera.h"
#include "tokens.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <inttypes.h>

static const char* TAG = "SD_CARD";
static const char* MOUNT_POINT = "/sdcard";

#define MAX_LOCAL_CLIPS 100
#define MAX_SD_PERCENT 80

static bool sd_ready = false;
static uint32_t sd_mount_attempts = 0;
static int sd_mounted_khz = 0;
static bool sd_probe_ok = false;
static char sd_error[128] = "";
static char sd_probe_path[128] = "";
static uint16_t clip_count = 0;

static const int mount_clocks_khz[] = {20000, 10000, 4000, 400};

esp_err_t sd_card_init(void) {
    if (sd_ready) {
        sdmmc_card_t* card;
        esp_err_t err = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
        if (err != ESP_OK) ESP_LOGW(TAG, "Unmount before reinit: %s", esp_err_to_name(err));
    }
    sd_ready = false;
    sd_probe_ok = false;
    sd_mounted_khz = 0;
    strcpy(sd_error, "Not mounted");

    for (unsigned i = 0; i < sizeof(mount_clocks_khz)/sizeof(mount_clocks_khz[0]); i++) {
        sd_mount_attempts++;
        ESP_LOGI(TAG, "SD_MOUNT_ATTEMPT attempt=%lu khz=%d format=0", (unsigned long)sd_mount_attempts, mount_clocks_khz[i]);

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = mount_clocks_khz[i];

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;  // 1-bit mode
        slot_config.clk = 14;
        slot_config.cmd = 15;
        slot_config.d0 = 2;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 0
        };

        sdmmc_card_t* card;
        esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "SD already mounted");
                sd_ready = true;
                sd_mounted_khz = mount_clocks_khz[i];
                strcpy(sd_error, "");
                break;
            }
            strcpy(sd_error, "Mount failed (format disabled)");
            continue;
        }

        sd_ready = true;
        sd_mounted_khz = mount_clocks_khz[i];
        strcpy(sd_error, "");

        // Crear directorios base
        struct stat st;
        if (stat("/sdcard/clips", &st) != 0) {
            if (mkdir("/sdcard/clips", 0755) != 0) {
                strcpy(sd_error, "Cannot create clips directory");
            }
        }
        if (stat("/sdcard/diagnostics", &st) != 0) {
            mkdir("/sdcard/diagnostics", 0755);
        }

        clip_count = sd_card_count_clips();

        ESP_LOGI(TAG, "SD_OK MB=%llu used=%llu clips=%u khz=%d",
                 (unsigned long long)(card->csd.capacity * card->csd.sector_size / 1024 / 1024),
                 (unsigned long long)sd_card_used_bytes(),
                 clip_count, sd_mounted_khz);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "SD_UNAVAILABLE attempts=%lu error=%s", (unsigned long)sd_mount_attempts, sd_error);
    return ESP_FAIL;
}

bool sd_card_probe(void) {
    sd_probe_ok = false;
    if (!sd_ready) { strcpy(sd_error, "Not mounted"); return false; }

    if (stat("/sdcard/diagnostics", NULL) != 0) {
        if (mkdir("/sdcard/diagnostics", 0755) != 0) {
            strcpy(sd_error, "Cannot create diagnostics directory");
            return false;
        }
    }

    char path[128];
    for (int i = 0; i < 4; i++) {
        uint32_t r = esp_random();
        snprintf(path, sizeof(path), "/sdcard/diagnostics/probe-%08lx.txt", (unsigned long)r);
        if (stat(path, NULL) != 0) break;
        path[0] = '\0';
    }
    if (!path[0]) { strcpy(sd_error, "Cannot allocate unique probe name"); return false; }

    const char* payload = "CAM_SD_WRITE_READ_PROBE_V1\n";
    size_t len = strlen(payload);
    FILE* f = fopen(path, "w");
    if (!f) { strcpy(sd_error, "Probe open failed"); return false; }
    strcpy(sd_probe_path, path);
    size_t written = fwrite(payload, 1, len, f);
    fclose(f);
    if (written != len) { strcpy(sd_error, "Probe short write"); return false; }

    f = fopen(path, "r");
    if (!f) { strcpy(sd_error, "Probe reopen failed"); return false; }
    char buf[64];
    size_t read = fread(buf, 1, len, f);
    fclose(f);
    if (read != len || memcmp(buf, payload, len) != 0) {
        strcpy(sd_error, "Probe read verification failed");
        return false;
    }

    sd_probe_ok = true;
    strcpy(sd_error, "");
    ESP_LOGI(TAG, "SD_PROBE_OK path=%s bytes=%u", path, (unsigned)len);
    return true;
}

void sd_card_report_json(char* buffer, size_t bufsize) {
    uint64_t total = 0, used = 0;
    if (sd_ready) {
        total = sd_card_total_bytes();
        used = sd_card_used_bytes();
    }
    snprintf(buffer, bufsize,
        "{\"ready\":%s,\"total_mb\":%llu,\"volume_mb\":%llu,\"used_mb\":%llu,"
        "\"clips\":%u,\"mount_attempts\":%lu,\"clock_khz\":%d,"
        "\"write_read_verified\":%s,\"probe_path\":\"%s\",\"error\":\"%s\","
        "\"format_on_failure\":false}",
        sd_ready ? "true" : "false",
        (unsigned long long)(total / 1024 / 1024),
        (unsigned long long)(total / 1024 / 1024),
        (unsigned long long)(used / 1024 / 1024),
        clip_count,
        (unsigned long)sd_mount_attempts,
        sd_mounted_khz,
        sd_probe_ok ? "true" : "false",
        sd_probe_path,
        sd_error
    );
}

esp_err_t sd_card_recover(void) {
    if (!sd_ready) {
        esp_err_t err = sd_card_init();
        if (err != ESP_OK) return err;
    }
    bool ok = sd_card_probe();
    if (!ok && sd_ready) {
        // Segundo intento tras reset de bus
        sdmmc_card_t* card;
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
        sd_ready = false;
        ok = sd_card_init() == ESP_OK && sd_card_probe();
    }
    return ok ? ESP_OK : ESP_FAIL;
}

bool sd_card_save_frame(const char* path, bool flash_enabled, uint32_t flash_duration_ms) {
    if (!sd_ready) return false;
    camera_fb_t* fb = camera_capture_fresh_frame(flash_enabled, flash_duration_ms);
    if (!fb) return false;
    FILE* f = fopen(path, "wb");
    bool ok = false;
    if (f) {
        ok = fwrite(fb->buf, 1, fb->len, f) == fb->len;
        fclose(f);
    }
    esp_camera_fb_return(fb);
    return ok;
}

uint16_t sd_card_count_clips(void) {
    if (!sd_ready) return 0;
    DIR* dir = opendir("/sdcard/clips");
    if (!dir) return 0;
    uint16_t n = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR && strncmp(ent->d_name, "clip-", 5) == 0) n++;
    }
    closedir(dir);
    return n;
}

uint16_t sd_card_prune_old_clips(uint16_t target) {
    if (!sd_ready) return 0;
    uint16_t removed = 0;
    while (clip_count >= target) {
        // Buscar clip más antiguo (menor número)
        DIR* dir = opendir("/sdcard/clips");
        if (!dir) break;
        char oldest[64] = "";
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "clip-", 5) == 0) {
                if (!oldest[0] || strcmp(ent->d_name, oldest) < 0) {
                    strncpy(oldest, ent->d_name, sizeof(oldest)-1);
                }
            }
        }
        closedir(dir);
        if (!oldest[0]) break;

        char full[128];
        snprintf(full, sizeof(full), "/sdcard/clips/%s", oldest);
        // Eliminar recursivamente
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", full);
        if (system(cmd) == 0) {
            removed++;
            clip_count = sd_card_count_clips();
        } else break;
    }

    // También por porcentaje
    uint64_t total = sd_card_total_bytes();
    uint64_t used = sd_card_used_bytes();
    while (sd_ready && total > 0 && (used * 100 / total) >= MAX_SD_PERCENT) {
        DIR* dir = opendir("/sdcard/clips");
        if (!dir) break;
        char oldest[64] = "";
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_DIR && strncmp(ent->d_name, "clip-", 5) == 0) {
                if (!oldest[0] || strcmp(ent->d_name, oldest) < 0) {
                    strncpy(oldest, ent->d_name, sizeof(oldest)-1);
                }
            }
        }
        closedir(dir);
        if (!oldest[0]) break;

        char full[128];
        snprintf(full, sizeof(full), "/sdcard/clips/%s", oldest);
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", full);
        if (system(cmd) == 0) {
            removed++;
            clip_count = sd_card_count_clips();
            used = sd_card_used_bytes();
        } else break;
    }
    return removed;
}

bool sd_card_is_ready(void) { return sd_ready; }

uint64_t sd_card_total_bytes(void) {
    if (!sd_ready) return 0;
    // Aproximado via statvfs
    struct statvfs vfs;
    if (statvfs("/sdcard", &vfs) == 0) {
        return (uint64_t)vfs.f_blocks * vfs.f_frsize;
    }
    return 0;
}

uint64_t sd_card_used_bytes(void) {
    if (!sd_ready) return 0;
    struct statvfs vfs;
    if (statvfs("/sdcard", &vfs) == 0) {
        return (uint64_t)(vfs.f_blocks - vfs.f_bfree) * vfs.f_frsize;
    }
    return 0;
}