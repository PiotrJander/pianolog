// Build: cp ../main/archive.c stubs/archive_copy.c && gcc -Istubs archive_test.c -o archive_test
// Host test for archive.c: moving takes to the card (with simulated power cuts) and the
// ring-buffer deletion when there is no card.
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app.h"

// ---- stubs ----
static bool card = true;
static uint64_t flash_cap = 0;          // simulated flash size; 0 = unlimited
static char live_day[16], live_file[40];
bool storage_ready(void) { return card; }
void storage_unmount(void) { card = false; }
bool flash_ready(void) { return true; }
void files_lock(void) {}
void files_unlock(void) {}
bool recorder_is_live(const char *d, const char *f) { return !strcmp(d, live_day) && !strcmp(f, live_file); }
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
static uint64_t used_bytes(const char *root)
{
    uint64_t sum = 0;
    DIR *r = opendir(root);
    if (!r) return 0;
    struct dirent *de;
    while ((de = readdir(r))) {
        if (de->d_name[0] == '.') continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", root, de->d_name);
        struct stat st; stat(p, &st);
        sum += S_ISDIR(st.st_mode) ? used_bytes(p) : (uint64_t)st.st_size;
    }
    closedir(r);
    return sum;
}
bool flash_info(uint64_t *t, uint64_t *f)
{
    uint64_t u = used_bytes(PL_REC_DIR);
    if (t) *t = flash_cap;
    if (f) *f = flash_cap > u ? flash_cap - u : 0;
    return true;
}

#include "archive_copy.c"

static void put(const char *path, size_t n, int seed)
{
    FILE *f = fopen(path, "wb");
    for (size_t i = 0; i < n; i++) fputc((int)((i * 31 + seed) & 0xFF), f);
    fclose(f);
}
static bool exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int count(const char *dir)
{
    int n = 0; DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d); return n;
}

int main(void)
{
    system("rm -rf t && mkdir -p t/flash/rec/2026-11-16 t/flash/rec/2026-11-17 t/sdcard/rec");
    put("t/flash/rec/2026-11-16/20-00-00_0100s_0300n.mid", 9000, 1);
    put("t/flash/rec/2026-11-17/09-00-00_0050s_0100n_star.mid", 5000, 2);
    put("t/flash/rec/2026-11-17/10-00-00.mid", 3000, 3);                  // being recorded
    strcpy(live_day, "2026-11-17"); strcpy(live_file, "10-00-00.mid");

    // Power cut 1: a .tmp left on the card from an interrupted copy.
    mkdir("t/sdcard/rec/2026-11-17", 0775);
    put("t/sdcard/rec/2026-11-17/09-00-00_0050s_0100n_star.mid.tmp", 1234, 9);
    // Power cut 2: the card copy was renamed into place but the flash copy not yet deleted.
    mkdir("t/sdcard/rec/2026-11-16", 0775);
    put("t/sdcard/rec/2026-11-16/20-00-00_0100s_0300n.mid", 9000, 1);

    archive_pass();
    assert(!exists("t/flash/rec/2026-11-16"));                                       // day emptied and removed
    assert(exists("t/sdcard/rec/2026-11-16/20-00-00_0100s_0300n.mid"));
    assert(!exists("t/sdcard/rec/2026-11-17/09-00-00_0050s_0100n_star.mid.tmp"));
    assert(exists("t/sdcard/rec/2026-11-17/09-00-00_0050s_0100n_star.mid"));
    assert(!exists("t/flash/rec/2026-11-17/09-00-00_0050s_0100n_star.mid"));
    assert(exists("t/flash/rec/2026-11-17/10-00-00.mid"));                           // live take untouched
    assert(count("t/sdcard/rec/2026-11-16") == 1);
    char path[200]; bool oncard;
    assert(take_resolve("2026-11-17", "10-00-00.mid", path, sizeof path, &oncard) && !oncard);
    assert(take_resolve("2026-11-16", "20-00-00_0100s_0300n.mid", path, sizeof path, &oncard) && oncard);
    printf("archive: moves, interrupted-move recovery OK\n");

    // A different file already on the card under the same name: both kept.
    put("t/flash/rec/2026-11-17/11-00-00_0010s_0020n.mid", 700, 4);
    put("t/sdcard/rec/2026-11-17/11-00-00_0010s_0020n.mid", 700, 5);
    archive_pass();
    assert(exists("t/sdcard/rec/2026-11-17/11-00-00_0010s_0020n-flash.mid"));
    printf("archive: name clash keeps both copies OK\n");

    // ---- no card: ring buffer ----
    card = false;
    system("rm -rf t/flash/rec && mkdir -p t/flash/rec/unsynced t/flash/rec/2026-11-10 t/flash/rec/2026-11-12");
    put("t/flash/rec/unsynced/b0003-001_0010s_0010n.mid", 300 * 1024, 1);
    put("t/flash/rec/2026-11-10/08-00-00_0010s_0010n_star.mid", 300 * 1024, 2);
    put("t/flash/rec/2026-11-10/09-00-00_0010s_0010n.mid", 300 * 1024, 3);
    put("t/flash/rec/2026-11-12/08-00-00_0010s_0010n.mid", 300 * 1024, 4);
    put("t/flash/rec/2026-11-12/10-00-00.mid", 300 * 1024, 5);              // live
    strcpy(live_day, "2026-11-12"); strcpy(live_file, "10-00-00.mid");
    flash_cap = 5 * 300 * 1024 + 200 * 1024;                               // 200 kB free: below low water
    flash_make_room();
    // Needs 1 MB free: deletes unsynced, then 2026-11-10/09 (skipping the starred one), then 2026-11-12/08.
    assert(!exists("t/flash/rec/unsynced"));
    assert(!exists("t/flash/rec/2026-11-10/09-00-00_0010s_0010n.mid"));
    assert(exists("t/flash/rec/2026-11-10/08-00-00_0010s_0010n_star.mid"));
    assert(!exists("t/flash/rec/2026-11-12/08-00-00_0010s_0010n.mid"));
    assert(exists("t/flash/rec/2026-11-12/10-00-00.mid"));
    uint64_t fr; flash_info(NULL, &fr); assert(fr >= 1024 * 1024);
    // Still short and only a starred take left besides the live one: the starred one goes.
    flash_cap = 2 * 300 * 1024 + 100 * 1024;
    flash_make_room();
    assert(!exists("t/flash/rec/2026-11-10/08-00-00_0010s_0010n_star.mid"));
    assert(exists("t/flash/rec/2026-11-12/10-00-00.mid"));
    // With a card nothing is deleted.
    card = true; flash_cap = 300 * 1024 + 10;
    put("t/flash/rec/2026-11-12/07-00-00_0010s_0010n.mid", 100, 1);
    flash_make_room();
    assert(exists("t/flash/rec/2026-11-12/07-00-00_0010s_0010n.mid"));
    printf("ring buffer: oldest first, starred last, live take kept, card disables it OK\n");
    system("rm -rf t");
    return 0;
}
