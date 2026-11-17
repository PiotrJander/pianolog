// Host test for cleanup.c.
// Build: cp ../main/cleanup.c stubs/cleanup_copy.c && gcc -Istubs cleanup_test.c -o cleanup_test
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app.h"
#include "cleanup_copy.c"

static void touch(const char *p) { FILE *f = fopen(p, "w"); assert(f); fputs("x", f); fclose(f); }
static bool exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

int main(void)
{
    system("rm -rf t && mkdir -p t/card/rec/2026-11-17/sub t/card/rec/my-best-takes t/card/rec/empty "
           "t/card/Photos/2025 t/card/.Trashes t/card/System\\ Volume\\ Information");
    touch("t/card/pianolog.txt");
    touch("t/card/notes.txt");
    touch("t/card/song.mid");                                   // .mid outside rec/: not ours
    touch("t/card/Photos/2025/img.jpg");
    touch("t/card/.Trashes/x");
    touch("t/card/System Volume Information/IndexerVolumeGuid");
    touch("t/card/rec/loose.mid");                              // file directly in rec/
    touch("t/card/rec/2026-11-17/20-31-05_0185s_0412n.mid");
    touch("t/card/rec/2026-11-17/20-58-12.mid");               // unfinished take
    touch("t/card/rec/2026-11-17/._20-31-05_0185s_0412n.mid"); // macOS metadata
    touch("t/card/rec/2026-11-17/x.mid.tmp");
    touch("t/card/rec/2026-11-17/readme.txt");
    touch("t/card/rec/2026-11-17/sub/a.mid");                  // nested folder: not ours
    touch("t/card/rec/my-best-takes/Concert.MID");             // renamed folder and file: kept
    touch("t/card/rec/my-best-takes/cover.png");
    for (int i = 0; i < 70; i++) { char p[64]; snprintf(p, sizeof p, "t/card/junk%02d.bin", i); touch(p); }

    card_cleanup("t/card");

    assert(exists("t/card/pianolog.txt"));
    assert(!exists("t/card/notes.txt") && !exists("t/card/song.mid") && !exists("t/card/Photos"));
    assert(!exists("t/card/.Trashes") && !exists("t/card/System Volume Information"));
    assert(!exists("t/card/junk00.bin") && !exists("t/card/junk69.bin"));
    assert(!exists("t/card/rec/loose.mid"));
    assert(exists("t/card/rec/2026-11-17/20-31-05_0185s_0412n.mid"));
    assert(exists("t/card/rec/2026-11-17/20-58-12.mid"));
    assert(!exists("t/card/rec/2026-11-17/._20-31-05_0185s_0412n.mid"));
    assert(!exists("t/card/rec/2026-11-17/x.mid.tmp") && !exists("t/card/rec/2026-11-17/readme.txt"));
    assert(!exists("t/card/rec/2026-11-17/sub"));
    assert(exists("t/card/rec/my-best-takes/Concert.MID") && !exists("t/card/rec/my-best-takes/cover.png"));
    assert(!exists("t/card/rec/empty"));
    printf("cleanup: foreign files removed, recordings kept OK\n");
    system("rm -rf t");
    return 0;
}
