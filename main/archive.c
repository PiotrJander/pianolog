// Moves closed takes from on-board flash to the SD card.
//
// For each closed take: copy to "<name>.tmp" on the card, fsync, read the copy back and
// compare it byte for byte with the original, rename it to its final name, and only then
// delete the original from flash. A power cut at any point leaves at least one complete copy:
//   * before the rename: the original is untouched; the stray .tmp is removed on the next pass
//   * after the rename, before the delete: both copies exist; the next pass sees an identical
//     file on the card and just deletes the flash copy.
// The card is therefore written only in a short burst after each take, never continuously.
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "archive";
static TaskHandle_t s_task;

#define CHUNK 4096

static bool is_mid(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && strcmp(name + l - 4, ".mid") == 0;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t l = strlen(s), k = strlen(suffix);
    return l >= k && strcmp(s + l - k, suffix) == 0;
}

static bool same_content(const char *a, const char *b, uint8_t *buf1, uint8_t *buf2)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    bool same = fa && fb;
    while (same) {
        size_t na = fread(buf1, 1, CHUNK, fa), nb = fread(buf2, 1, CHUNK, fb);
        if (na != nb || memcmp(buf1, buf2, na) != 0) same = false;
        if (na == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static bool copy_file(const char *from, const char *to, uint8_t *buf)
{
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    bool ok = true;
    size_t n;
    while (ok && (n = fread(buf, 1, CHUNK, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    ok = ok && !ferror(in) && fflush(out) == 0 && fsync(fileno(out)) == 0;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

// Returns false if the card failed (stop the pass).
static bool move_one(const char *day, const char *name, uint8_t *b1, uint8_t *b2)
{
    char src[PL_PATH_MAX + 48], dst[PL_PATH_MAX + 48], tmp[PL_PATH_MAX + 56], dir[PL_PATH_MAX];
    snprintf(src, sizeof src, PL_REC_DIR "/%s/%s", day, name);
    snprintf(dir, sizeof dir, PL_CARD_REC "/%s", day);
    snprintf(dst, sizeof dst, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    mkdir(PL_CARD_REC, 0775);
    mkdir(dir, 0775);

    struct stat st;
    if (stat(dst, &st) == 0) {
        // Left over from a move interrupted after the rename.
        if (same_content(src, dst, b1, b2)) {
            unlink(src);
            return true;
        }
        ESP_LOGW(TAG, "%s/%s differs from the card copy, keeping both", day, name);
        snprintf(dst, sizeof dst, "%s/%.*s-flash.mid", dir, (int)(strlen(name) - 4), name);
        snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    }
    if (!copy_file(src, tmp, b1)) {
        ESP_LOGW(TAG, "copy of %s/%s failed (errno %d)", day, name, errno);
        unlink(tmp);
        return false;
    }
    if (!same_content(src, tmp, b1, b2)) {
        ESP_LOGW(TAG, "verification of %s/%s failed, will retry", day, name);
        unlink(tmp);
        return false;
    }
    if (rename(tmp, dst) != 0) {
        unlink(tmp);
        return false;
    }
    unlink(src);
    ESP_LOGI(TAG, "moved %s/%s to the card", day, name);
    return true;
}

static void remove_stray_tmp(const char *day)
{
    char dir[PL_PATH_MAX];
    snprintf(dir, sizeof dir, PL_CARD_REC "/%s", day);
    DIR *d = opendir(dir);
    if (!d) return;
    char names[8][48];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 8)
        if (ends_with(e->d_name, ".tmp") && strlen(e->d_name) < 48) strcpy(names[n++], e->d_name);
    closedir(d);
    for (int i = 0; i < n; i++) {
        char p[PL_PATH_MAX + 50];
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        unlink(p);
    }
}

static void archive_pass(void)
{
    if (!storage_ready() || !flash_ready()) return;
    uint8_t *b1 = malloc(CHUNK), *b2 = malloc(CHUNK);
    if (!b1 || !b2) { free(b1); free(b2); return; }

    // Collect the day folders first, then work through them one by one.
    char days[32][16];
    int nd = 0;
    DIR *root = opendir(PL_REC_DIR);
    if (root) {
        struct dirent *de;
        while ((de = readdir(root)) != NULL && nd < 32)
            if (de->d_name[0] != '.' && strlen(de->d_name) < 16) strcpy(days[nd++], de->d_name);
        closedir(root);
    }
    bool card_ok = true;
    for (int i = 0; i < nd && card_ok; i++) {
        files_lock();
        remove_stray_tmp(days[i]);
        char dir[PL_PATH_MAX];
        snprintf(dir, sizeof dir, PL_REC_DIR "/%s", days[i]);
        char names[16][40];
        int nf = 0;
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *fe;
            while ((fe = readdir(d)) != NULL && nf < 16) {
                // Only closed takes: they carry duration and note count in their name.
                if (is_mid(fe->d_name) && strlen(fe->d_name) < 40 &&
                    take_name_parse(fe->d_name, NULL, NULL, NULL) && !recorder_is_live(days[i], fe->d_name))
                    strcpy(names[nf++], fe->d_name);
            }
            closedir(d);
        }
        files_unlock();
        // One file per lock hold, so the web page never waits long.
        for (int k = 0; k < nf && card_ok; k++) {
            files_lock();
            card_ok = move_one(days[i], names[k], b1, b2);
            files_unlock();
        }
        files_lock();
        rmdir(dir);                     // only succeeds once the day is empty
        files_unlock();
        if (nf == 16) i--;              // more files than fit in one batch: same day again
    }
    free(b1);
    free(b2);
    // A failed copy usually means the card was pulled out: drop it, main.c remounts it later.
    if (!card_ok) storage_unmount();
}

static void archive_task(void *arg)
{
    for (;;) {
        // Wake on a kick, or every 30 s to notice a card that was just inserted.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
        vTaskDelay(pdMS_TO_TICKS(1000));
        archive_pass();
    }
}

void archive_start(void)
{
    xTaskCreate(archive_task, "archive", 6144, NULL, 3, &s_task);
}

void archive_kick(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

bool take_resolve(const char *day, const char *file, char *path, size_t cap, bool *on_card)
{
    struct stat st;
    snprintf(path, cap, PL_REC_DIR "/%s/%s", day, file);
    if (stat(path, &st) == 0) { if (on_card) *on_card = false; return true; }
    if (storage_ready()) {
        snprintf(path, cap, PL_CARD_REC "/%s/%s", day, file);
        if (stat(path, &st) == 0) { if (on_card) *on_card = true; return true; }
    }
    return false;
}

// ---- ring-buffer behaviour without a card -----------------------------------------------
// With no usable card and the flash nearly full, delete the oldest closed takes until there
// is room again. Starred takes go last: only when nothing else is left to delete.

#define LOW_WATER   (512 * 1024)
#define HIGH_WATER  (1024 * 1024)

// "unsynced" takes were recorded before the clock was set; treat that folder as the oldest.
static int day_order(const char *a, const char *b)
{
    bool ua = !strcmp(a, "unsynced"), ub = !strcmp(b, "unsynced");
    if (ua != ub) return ua ? -1 : 1;
    return strcmp(a, b);
}

// Finds the oldest deletable take. Returns false when there is none.
static bool find_oldest(bool allow_star, char *day, char *file)
{
    bool found = false;
    DIR *root = opendir(PL_REC_DIR);
    if (!root) return false;
    struct dirent *de;
    while ((de = readdir(root)) != NULL) {
        if (de->d_name[0] == '.' || strlen(de->d_name) >= 16) continue;
        if (found && day_order(de->d_name, day) > 0) continue;
        char dir[PL_PATH_MAX];
        snprintf(dir, sizeof dir, PL_REC_DIR "/%s", de->d_name);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *fe;
        while ((fe = readdir(d)) != NULL) {
            bool star;
            if (!is_mid(fe->d_name) || strlen(fe->d_name) >= 40) continue;
            if (!take_name_parse(fe->d_name, NULL, NULL, &star)) continue;   // open or unrepaired
            if (star && !allow_star) continue;
            if (recorder_is_live(de->d_name, fe->d_name)) continue;
            int o = found ? day_order(de->d_name, day) : -1;
            if (o < 0 || (o == 0 && strcmp(fe->d_name, file) < 0)) {
                strcpy(day, de->d_name);
                strcpy(file, fe->d_name);
                found = true;
            }
        }
        closedir(d);
    }
    closedir(root);
    return found;
}

void flash_make_room(void)
{
    if (storage_ready() || !flash_ready()) return;     // with a card, the archiver frees space
    uint64_t free_b = 0;
    if (!flash_info(NULL, &free_b) || free_b >= LOW_WATER) return;
    files_lock();
    int removed = 0;
    while (flash_info(NULL, &free_b) && free_b < HIGH_WATER && removed < 200) {
        char day[16], file[40];
        if (!find_oldest(false, day, file) && !find_oldest(true, day, file)) break;
        char p[PL_PATH_MAX + 48];
        snprintf(p, sizeof p, PL_REC_DIR "/%s/%s", day, file);
        if (unlink(p) != 0) break;
        snprintf(p, sizeof p, PL_REC_DIR "/%s", day);
        rmdir(p);
        removed++;
        ESP_LOGW(TAG, "flash full and no card: deleted oldest take %s/%s", day, file);
    }
    files_unlock();
}
