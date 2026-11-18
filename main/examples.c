// A few example takes so a new recorder doesn't open on an empty page.
//
// The pieces come from tools/make-examples.py (public-domain music, notes entered by hand) and
// are embedded as one blob: per file a NUL-terminated name, a 4-byte little-endian length and
// the MIDI bytes, ended by an empty name. They are written once into the "examples" folder in
// flash; an NVS flag records that, so takes the player deletes stay deleted.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "nvs.h"
#include "app.h"

static const char *TAG = "examples";

extern const uint8_t examples_start[] asm("_binary_examples_bin_start");
extern const uint8_t examples_end[] asm("_binary_examples_bin_end");

#define EXAMPLES_DIR PL_REC_DIR "/examples"

void examples_install(void)
{
    if (!flash_ready()) return;
    nvs_handle_t h;
    if (nvs_open("pianolog", NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t done = 0;
    nvs_get_u8(h, "examples", &done);
    if (done) { nvs_close(h); return; }

    files_lock();
    mkdir(PL_REC_DIR, 0775);
    mkdir(EXAMPLES_DIR, 0775);
    bool ok = true;
    int count = 0;
    const uint8_t *p = examples_start;
    while (ok && p < examples_end && *p) {
        const char *name = (const char *)p;
        size_t nl = strnlen(name, (size_t)(examples_end - p));
        if (p + nl + 5 > examples_end) { ok = false; break; }
        const uint8_t *lp = p + nl + 1;
        uint32_t len = lp[0] | lp[1] << 8 | lp[2] << 16 | (uint32_t)lp[3] << 24;
        const uint8_t *data = lp + 4;
        if (data + len > examples_end) { ok = false; break; }

        char path[PL_PATH_MAX + 48];
        snprintf(path, sizeof path, EXAMPLES_DIR "/%s", name);
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(data, 1, len, f) == len;
        if (f) ok = (fclose(f) == 0) && ok;
        count += ok;
        p = data + len;
    }
    files_unlock();

    if (ok) {
        nvs_set_u8(h, "examples", 1);           // a power cut before this just writes them again
        nvs_commit(h);
        ESP_LOGI(TAG, "%d example takes added", count);
    } else {
        ESP_LOGW(TAG, "could not write the example takes");
    }
    nvs_close(h);
}
