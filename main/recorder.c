// Turns the stream of MIDI messages into one Standard MIDI File per take.
//
// A take starts with the first Note On after silence and ends after g_cfg.gap_s seconds
// without any event. Takes are written to on-board flash (LittleFS). While a take is open the
// file has its MTrk length set to 0 and no End-of-Track; data is synced every 2 s, so a power
// cut loses at most the last two seconds. Unfinished files are completed at the next boot
// (recorder_repair_dir) and on the fly when they are downloaded (web.c). Closed takes are
// moved to the SD card by archive.c.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"
#include "smf.h"

static const char *TAG = "rec";

#define SYNC_INTERVAL_US  (2 * 1000 * 1000)
#define BUF_SIZE          4096

typedef struct {
    int64_t  t_us;
    uint8_t  src;
    uint16_t len;
    uint8_t  data[3];
    uint8_t *sysex;         // malloc'ed when len > 3, freed by the recorder task
} rec_ev_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_lock;
static pl_rec_status_t s_st;
static uint32_t s_boot_count;
static uint32_t s_dropped;

// state owned by the recorder task
static int      s_fd = -1;
static char     s_dir[PL_PATH_MAX];
static char     s_base[24];
static uint8_t  s_buf[BUF_SIZE];
static size_t   s_buf_len;
static uint32_t s_trk_bytes;        // track bytes produced so far (buffer included)
static int64_t  s_start_us, s_last_ev_us, s_last_sync_us;
static uint32_t s_last_tick;
static uint32_t s_notes;
static uint8_t  s_pedal[16];        // last CC64 value per channel, tracked while idle too

// ---- file names -------------------------------------------------------------------------

bool take_name_parse(const char *name, int *dur_s, int *notes, bool *star)
{
    int d = 0, n = 0;
    const char *us = strchr(name, '_');
    bool final = us && sscanf(us, "_%ds_%dn", &d, &n) == 2;
    if (dur_s) *dur_s = final ? d : -1;
    if (notes) *notes = final ? n : -1;
    if (star)  *star = us && strstr(us, "_star") != NULL;
    return final;
}

static bool is_mid(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && strcmp(name + l - 4, ".mid") == 0;
}

// ---- low-level writing ------------------------------------------------------------------

static bool flush_buf(void)
{
    size_t off = 0;
    while (off < s_buf_len) {
        ssize_t w = write(s_fd, s_buf + off, s_buf_len - off);
        if (w <= 0) {
            ESP_LOGE(TAG, "write failed: errno %d", errno);
            return false;
        }
        off += (size_t)w;
    }
    s_buf_len = 0;
    return true;
}

static bool put(const uint8_t *p, size_t n)
{
    if (n > BUF_SIZE) return false;
    if (s_buf_len + n > BUF_SIZE && !flush_buf()) return false;
    memcpy(s_buf + s_buf_len, p, n);
    s_buf_len += n;
    s_trk_bytes += (uint32_t)n;
    return true;
}

static void set_status_idle(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.recording = false;
    s_st.day[0] = s_st.file[0] = 0;
    s_st.elapsed_s = s_st.notes = 0;
    xSemaphoreGive(s_lock);
}

// Writing failed (flash full, most likely). What reached the flash stays and is repaired at
// the next boot; the next note starts a fresh take.
static void abort_take(void)
{
    ESP_LOGE(TAG, "write error, take ended early (flash full?)");
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
    s_buf_len = 0;
    set_status_idle();
}

// ---- take lifecycle ---------------------------------------------------------------------

static bool open_take(int64_t t_us)
{
    if (!flash_ready()) return false;
    flash_make_room();

    char day[16];
    static uint32_t seq;
    if (time_is_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(day, sizeof day, "%Y-%m-%d", &tm);
        strftime(s_base, sizeof s_base, "%H-%M-%S", &tm);
    } else {                // clock not set yet: file by boot number, never overwriting
        strcpy(day, "unsynced");
        snprintf(s_base, sizeof s_base, "b%04u-%03u", (unsigned)(s_boot_count % 10000), (unsigned)(++seq % 1000));
    }
    snprintf(s_dir, sizeof s_dir, PL_REC_DIR "/%s", day);
    mkdir(PL_REC_DIR, 0775);
    mkdir(s_dir, 0775);

    char path[PL_PATH_MAX + 48];
    snprintf(path, sizeof path, "%s/%s.mid", s_dir, s_base);
    s_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "cannot create %s: errno %d", path, errno);
        abort_take();
        return false;
    }

    uint8_t tmp[160];
    size_t n = smf_write_header(tmp);
    s_buf_len = 0;
    memcpy(s_buf, tmp, n);
    s_buf_len = n;
    s_trk_bytes = 0;
    n = smf_encode_tempo_60bpm(tmp);
    put(tmp, n);
    char title[64];
    snprintf(title, sizeof title, "pianolog %s %s", day, s_base);
    n = smf_encode_text(tmp, 0, 0x03, title);       // track name
    put(tmp, n);

    s_start_us = t_us;
    s_last_ev_us = t_us;
    s_last_sync_us = esp_timer_get_time();
    s_last_tick = 0;
    s_notes = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.recording = true;
    strlcpy(s_st.day, day, sizeof s_st.day);
    snprintf(s_st.file, sizeof s_st.file, "%s.mid", s_base);
    s_st.elapsed_s = 0;
    s_st.notes = 0;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "take started: %s", path);

    // A pedal that was already down before the first note belongs to the take.
    for (int ch = 0; ch < 16; ch++) {
        if (s_pedal[ch] >= 64) {
            uint8_t cc[3] = { (uint8_t)(0xB0 | ch), 64, s_pedal[ch] };
            n = smf_encode_channel(tmp, 0, cc, 3);
            put(tmp, n);
        }
    }
    return true;
}

static void write_event(const rec_ev_t *ev)
{
    int64_t rel = ev->t_us - s_start_us;
    if (rel < 0) rel = 0;
    uint32_t tick = (uint32_t)(rel / 1000);
    if (tick < s_last_tick) tick = s_last_tick;     // two inputs can deliver slightly out of order
    uint32_t delta = tick - s_last_tick;

    bool ok;
    if (ev->len <= 3) {
        uint8_t tmp[SMF_EVENT_MAX];
        size_t n = smf_encode_channel(tmp, delta, ev->data, ev->len);
        ok = put(tmp, n);
        if ((ev->data[0] & 0xF0) == 0x90 && ev->data[2] > 0) s_notes++;
    } else {
        uint8_t *tmp = malloc(ev->len + 8);
        if (!tmp) return;
        size_t n = smf_encode_sysex(tmp, delta, ev->sysex, ev->len);
        ok = n ? put(tmp, n) : true;
        free(tmp);
    }
    if (!ok) { abort_take(); return; }
    s_last_tick = tick;
    s_last_ev_us = ev->t_us;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.elapsed_s = tick / 1000;
    s_st.notes = s_notes;
    xSemaphoreGive(s_lock);
}

static void sync_take(void)
{
    if (s_buf_len == 0) return;
    if (!flush_buf() || fsync(s_fd) != 0) { abort_take(); return; }
    s_last_sync_us = esp_timer_get_time();
}

static void close_take(void)
{
    uint8_t tmp[8];
    size_t n = smf_encode_eot(tmp, 0);
    bool ok = put(tmp, n) && flush_buf();
    if (ok) {
        uint8_t len[4];
        smf_put_be32(len, s_trk_bytes);
        ok = lseek(s_fd, SMF_TRKLEN_OFF, SEEK_SET) == SMF_TRKLEN_OFF && write(s_fd, len, 4) == 4 && fsync(s_fd) == 0;
    }
    if (!ok) { abort_take(); return; }
    close(s_fd);
    s_fd = -1;

    char from[PL_PATH_MAX + 48], to[PL_PATH_MAX + 80];
    snprintf(from, sizeof from, "%s/%s.mid", s_dir, s_base);
    if (s_notes == 0) {
        unlink(from);       // cannot normally happen: a take starts with a note
    } else {
        snprintf(to, sizeof to, "%s/%s_%04us_%04un.mid", s_dir, s_base,
                 (unsigned)((s_last_tick + 500) / 1000), (unsigned)s_notes);
        if (rename(from, to) != 0) ESP_LOGW(TAG, "rename failed: errno %d", errno);
        ESP_LOGI(TAG, "take closed: %s", to);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.takes_since_boot++;
    xSemaphoreGive(s_lock);
    set_status_idle();
    archive_kick();
}

static void handle_event(const rec_ev_t *ev)
{
    uint8_t st = ev->len <= 3 ? ev->data[0] : 0xF0;
    if ((st & 0xF0) == 0xB0 && ev->data[1] == 64) s_pedal[st & 0x0F] = ev->data[2];

    if (s_fd < 0) {
        bool note_on = (st & 0xF0) == 0x90 && ev->data[2] > 0;
        if (!note_on) return;                       // only a note starts a take
        if (!open_take(ev->t_us)) return;
    }
    write_event(ev);
}

static volatile bool s_finish_req;
static SemaphoreHandle_t s_finished;

static void recorder_task(void *arg)
{
    rec_ev_t ev;
    for (;;) {
        if (s_finish_req) {             // restart pending (OTA): close cleanly, accept nothing more
            if (s_fd >= 0) close_take();
            xSemaphoreGive(s_finished);
            for (;;) {
                if (xQueueReceive(s_q, &ev, portMAX_DELAY) == pdTRUE && ev.sysex) free(ev.sysex);
            }
        }
        if (xQueueReceive(s_q, &ev, pdMS_TO_TICKS(250)) == pdTRUE) {
            handle_event(&ev);
            if (ev.sysex) free(ev.sysex);
        }
        if (s_fd < 0) continue;
        int64_t now = esp_timer_get_time();
        if (now - s_last_ev_us > (int64_t)g_cfg.gap_s * 1000000) close_take();
        else if (now - s_last_sync_us > SYNC_INTERVAL_US) {
            sync_take();
            static int n;
            if (++n % 15 == 0) flash_make_room();      // every ~30 s during long takes
        }
    }
}

// ---- public API -------------------------------------------------------------------------

void recorder_submit(pl_src_t src, int64_t t_us, const uint8_t *msg, size_t len)
{
    if (!s_q || len < 2) return;
    rec_ev_t ev = { .t_us = t_us, .src = (uint8_t)src, .len = (uint16_t)len, .sysex = NULL };
    if (len <= 3) {
        memcpy(ev.data, msg, len);
    } else {
        ev.sysex = malloc(len);
        if (!ev.sysex) return;
        memcpy(ev.sysex, msg, len);
    }
    if (xQueueSend(s_q, &ev, 0) != pdTRUE) {
        if (ev.sysex) free(ev.sysex);
        s_dropped++;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (src == SRC_USB) s_st.events_usb++; else s_st.events_din++;
    xSemaphoreGive(s_lock);
}

void recorder_get_status(pl_rec_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

bool recorder_finish_for_restart(int timeout_ms)
{
    if (!s_finished) return true;
    s_finish_req = true;
    return xSemaphoreTake(s_finished, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool recorder_is_live(const char *day, const char *file)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool live = s_st.recording && strcmp(day, s_st.day) == 0 && strcmp(file, s_st.file) == 0;
    xSemaphoreGive(s_lock);
    return live;
}

void recorder_start(uint32_t boot_count)
{
    s_boot_count = boot_count;
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(512, sizeof(rec_ev_t));
    s_finished = xSemaphoreCreateBinary();
    xTaskCreate(recorder_task, "recorder", 6144, NULL, 6, NULL);
}

// ---- repair of takes interrupted by a power cut -----------------------------------------

typedef struct { FILE *f; } scan_ctx_t;
static int scan_rd(void *ctx) { return fgetc(((scan_ctx_t *)ctx)->f); }

static void repair_file(bool live_check, const char *day, const char *dir, const char *name)
{
    if (live_check && recorder_is_live(day, name)) return;        // being written right now, not broken
    char path[PL_PATH_MAX + 48], to[PL_PATH_MAX + 80];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    scan_ctx_t c = { fopen(path, "rb") };
    if (!c.f) return;
    smf_scan_t sc;
    smf_scan(scan_rd, &c, &sc);
    fclose(c.f);

    if (!sc.header_ok || sc.notes == 0) {
        ESP_LOGW(TAG, "repair: removing empty/unreadable %s", path);
        unlink(path);
        return;
    }
    if (!sc.has_eot || sc.trk_len != sc.valid_end - SMF_HEADER_LEN) {
        if (truncate(path, sc.valid_end) != 0) ESP_LOGW(TAG, "repair: truncate errno %d", errno);
        int fd = open(path, O_WRONLY);
        if (fd < 0) return;
        uint32_t trk = sc.valid_end - SMF_HEADER_LEN;
        uint8_t tmp[8];
        if (!sc.has_eot) {
            size_t n = smf_encode_eot(tmp, 0);
            lseek(fd, 0, SEEK_END);
            if (write(fd, tmp, n) == (ssize_t)n) trk += (uint32_t)n;
        }
        smf_put_be32(tmp, trk);
        lseek(fd, SMF_TRKLEN_OFF, SEEK_SET);
        if (write(fd, tmp, 4) != 4) ESP_LOGW(TAG, "repair: length patch failed");
        fsync(fd);
        close(fd);
    }
    char base[24];
    size_t bl = strcspn(name, "_.");
    if (bl >= sizeof base) bl = sizeof base - 1;
    memcpy(base, name, bl);
    base[bl] = 0;
    snprintf(to, sizeof to, "%s/%s_%04us_%04un.mid", dir, base,
             (unsigned)((sc.ticks + 500) / 1000), (unsigned)sc.notes);
    rename(path, to);
    ESP_LOGI(TAG, "repaired %s", to);
}

void recorder_repair_dir(const char *rootdir)
{
    bool live_check = strcmp(rootdir, PL_REC_DIR) == 0;
    files_lock();
    DIR *root = opendir(rootdir);
    if (!root) { files_unlock(); return; }
    struct dirent *de;
    while ((de = readdir(root)) != NULL) {
        if (de->d_name[0] == '.' || strlen(de->d_name) > 12) continue;
        char day[16], dir[PL_PATH_MAX];
        strlcpy(day, de->d_name, sizeof day);
        snprintf(dir, sizeof dir, "%s/%s", rootdir, day);
        DIR *d = opendir(dir);
        if (!d) continue;
        // Collect first, fix afterwards: renaming while iterating confuses readdir.
        char pending[8][40];
        int np = 0;
        struct dirent *fe;
        while ((fe = readdir(d)) != NULL && np < 8) {
            if (is_mid(fe->d_name) && strlen(fe->d_name) < 40 && !take_name_parse(fe->d_name, NULL, NULL, NULL))
                strcpy(pending[np++], fe->d_name);
        }
        closedir(d);
        for (int i = 0; i < np; i++) repair_file(live_check, day, dir, pending[i]);
    }
    closedir(root);
    files_unlock();
}
