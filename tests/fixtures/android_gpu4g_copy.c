/* MIT. Exercise the real offline copier against isolated temporary files. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
/* Darwin exposes the same nanosecond timestamp with a different field name. */
#define st_mtim st_mtimespec
#endif
#include "android_gpu4g_files.h"

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    return owned_copy(argv[1], argv[2], 0600, getuid()) ? 1 : 0;
}
