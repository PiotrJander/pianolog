// Two stores:
//  * on-board flash, LittleFS: every take is recorded here. LittleFS is copy-on-write, so a
//    power cut can lose the last unsynced bytes but never corrupts the file system.
//  * microSD card, FAT32: long-term archive. Only touched in short bursts when a closed take
//    is moved over (archive.c), never continuously during a take.
//
// microSD on the ESP32-P4-NANO: SDMMC slot 0, 4-bit, fixed pins GPIO39..44.
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#if CONFIG_PL_SD_LDO_CHAN > 0
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include "app.h"

static const char *TAG = "sd";
static sdmmc_card_t *s_card;
static volatile bool s_ready;
static SemaphoreHandle_t s_lock;

static void lock(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

esp_err_t storage_mount(void)
{
    lock();
    if (s_ready) { xSemaphoreGive(s_lock); return ESP_OK; }

#if CONFIG_PL_SD_EN_GPIO >= 0
    gpio_config_t en = { .pin_bit_mask = 1ULL << CONFIG_PL_SD_EN_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&en);
    gpio_set_level(CONFIG_PL_SD_EN_GPIO, 0);        // active low
#endif

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;                  // slot 1 belongs to the Wi-Fi co-processor
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;         // 20 MHz is plenty for MIDI

#if CONFIG_PL_SD_LDO_CHAN > 0
    static sd_pwr_ctrl_handle_t pwr;
    if (!pwr) {
        sd_pwr_ctrl_ldo_config_t ldo = { .ldo_chan_id = CONFIG_PL_SD_LDO_CHAN };
        esp_err_t e = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &pwr);
        if (e != ESP_OK) ESP_LOGW(TAG, "on-chip LDO %d: %s", CONFIG_PL_SD_LDO_CHAN, esp_err_to_name(e));
    }
    if (pwr) host.pwr_ctrl_handle = pwr;
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = 43; slot.cmd = 44;
    slot.d0 = 39; slot.d1 = 40; slot.d2 = 41; slot.d3 = 42;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(PL_MOUNT, &host, &slot, &mcfg, &s_card);
    if (err == ESP_OK) {
        s_ready = true;
        mkdir(PL_CARD_REC, 0775);
        ESP_LOGI(TAG, "card mounted (%llu MB)",
                 (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "no usable card: %s (card must be formatted FAT32)", esp_err_to_name(err));
    }
    xSemaphoreGive(s_lock);
    return err;
}

void storage_unmount(void)
{
    lock();
    if (s_ready) {
        s_ready = false;
        esp_vfs_fat_sdcard_unmount(PL_MOUNT, s_card);
        s_card = NULL;
    }
    xSemaphoreGive(s_lock);
}

bool storage_ready(void) { return s_ready; }

bool storage_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_ready) return false;
    return esp_vfs_fat_info(PL_MOUNT, total_bytes, free_bytes) == ESP_OK;
}

// ---- on-board flash ---------------------------------------------------------------------

static bool s_flash;

esp_err_t flash_mount(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = PL_FLASH,
        .partition_label = "storage",
        .format_if_mount_failed = true,     // first boot, or a partition table change
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash storage unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_flash = true;
    files_lock();       // creates the lock here, before any other task can race for it
    files_unlock();
    mkdir(PL_REC_DIR, 0775);
    uint64_t total = 0, free_b = 0;
    flash_info(&total, &free_b);
    ESP_LOGI(TAG, "flash storage mounted, %llu of %llu kB free",
             (unsigned long long)(free_b >> 10), (unsigned long long)(total >> 10));
    return ESP_OK;
}

bool flash_ready(void) { return s_flash; }

bool flash_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    size_t total = 0, used = 0;
    if (!s_flash || esp_littlefs_info("storage", &total, &used) != ESP_OK) return false;
    if (total_bytes) *total_bytes = total;
    if (free_bytes) *free_bytes = total > used ? total - used : 0;
    return true;
}

static SemaphoreHandle_t s_files;

void files_lock(void)
{
    if (!s_files) s_files = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(s_files, portMAX_DELAY);
}

void files_unlock(void) { xSemaphoreGiveRecursive(s_files); }
