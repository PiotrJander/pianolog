// Removes everything from the SD card that pianolog did not put there, at boot.
//
// Kept:
//   /pianolog.txt
//   /rec/<folder>/<name>.mid       any .mid file in any folder directly under /rec
// Deleted:
//   any other file or folder in the card's top level (including hidden ones such as
//   ".Trashes", ".Spotlight-V100", "System Volume Information")
//   any file directly in /rec, and any non-.mid file or sub-folder inside a day folder
//   macOS "._*" AppleDouble files, even when they end in .mid
//   folders under /rec that end up empty
//
// .mid files inside day folders are kept even when their name or folder does not match
// what the recorder writes: a renamed recording is still a recording, and a bug in a name
// check must never be able to delete music. Disable the whole clean-up with clean_card=0 in
// pianolog.txt. Portable POSIX code, host-tested in test/cleanup_test.c.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "app.h"

static const char *TAG = "cleanup";

#define MAX_DEPTH 8

typedef struct { int files, dirs, failed; } stats_t;

static bool is_dot(const char *n) { return !strcmp(n, ".") || !strcmp(n, ".."); }

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t l = strlen(s), k = strlen(suffix);
    return l >= k && strcasecmp(s + l - k, suffix) == 0;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Deletes a file or a whole folder tree.
static void remove_tree(const char *path, int depth, stats_t *st)
{
    if (!is_dir(path)) {
        if (unlink(path) == 0) st->files++;
        else { st->failed++; ESP_LOGW(TAG, "could not delete %s", path); }
        return;
    }
    if (depth < MAX_DEPTH) {
        // Read a batch of names, close the folder, then delete: deleting while a folder is
        // open for reading is not reliable on every file system.
        for (;;) {
            char names[16][64];
            int n = 0;
            DIR *d = opendir(path);
            if (!d) break;
            struct dirent *e;
            while ((e = readdir(d)) != NULL && n < 16)
                if (!is_dot(e->d_name) && strlen(e->d_name) < 64) strcpy(names[n++], e->d_name);
            closedir(d);
            if (n == 0) break;
            int before = st->files + st->dirs;
            for (int i = 0; i < n; i++) {
                char child[256];
                snprintf(child, sizeof child, "%s/%s", path, names[i]);
                remove_tree(child, depth + 1, st);
            }
            if (st->files + st->dirs == before) break;      // nothing could be deleted: stop
        }
    }
    if (rmdir(path) == 0) st->dirs++;
    else { st->failed++; ESP_LOGW(TAG, "could not delete folder %s", path); }
}

static bool keep_in_day(const char *name)
{
    return strncmp(name, "._", 2) != 0 && ends_with_ci(name, ".mid");
}

// Collects up to 32 names in `dir` for which want() says "delete", then deletes them.
// Repeats while something was deleted, so folders with many entries are handled in batches.
typedef bool (*victim_fn)(const char *dir, const char *name);

static void delete_matching(const char *dir, victim_fn want, int depth, stats_t *st)
{
    for (;;) {
        char victims[32][64];
        int n = 0;
        DIR *d = opendir(dir);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < 32)
            if (!is_dot(e->d_name) && strlen(e->d_name) < 64 && want(dir, e->d_name)) strcpy(victims[n++], e->d_name);
        closedir(d);
        int before = st->files + st->dirs;
        for (int i = 0; i < n; i++) {
            char p[256];
            snprintf(p, sizeof p, "%s/%s", dir, victims[i]);
            ESP_LOGI(TAG, "removing %s", p);
            remove_tree(p, depth, st);
        }
        if (n < 32 || st->files + st->dirs == before) return;
    }
}

static bool child_is_dir(const char *dir, const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return is_dir(p);
}

static bool top_victim(const char *dir, const char *name)
{
    return strcasecmp(name, "pianolog.txt") != 0 && strcasecmp(name, "rec") != 0;
}

static bool rec_victim(const char *dir, const char *name) { return !child_is_dir(dir, name); }

static bool day_victim(const char *dir, const char *name)
{
    return child_is_dir(dir, name) || !keep_in_day(name);
}

void card_cleanup(const char *root)
{
    stats_t st = { 0 };
    char rec[128];
    snprintf(rec, sizeof rec, "%s/rec", root);

    delete_matching(root, top_victim, 1, &st);         // top level: only pianolog.txt and rec/
    if (is_dir(rec)) {
        delete_matching(rec, rec_victim, 2, &st);      // loose files in rec/
        char dirs[64][64];
        int nd = 0;
        DIR *d = opendir(rec);
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL && nd < 64)
            if (!is_dot(e->d_name) && strlen(e->d_name) < 64) strcpy(dirs[nd++], e->d_name);
        if (d) closedir(d);
        for (int i = 0; i < nd; i++) {
            char p[256];
            snprintf(p, sizeof p, "%s/%s", rec, dirs[i]);
            delete_matching(p, day_victim, 3, &st);    // inside a day: only .mid files stay
            if (rmdir(p) == 0) st.dirs++;              // succeeds only when nothing is left
        }
    }
    if (st.files || st.dirs || st.failed)
        ESP_LOGI(TAG, "card clean-up: %d files and %d folders removed, %d could not be removed",
                 st.files, st.dirs, st.failed);
}
