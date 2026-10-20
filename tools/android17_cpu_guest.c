/* MIT. PID-1 probe inside the guest only. Never an Android host program. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cpu_probe.generated.h"

int main(void) {
    /* All mount/reboot syscalls below are guest-only. Refuse accidental host execution. */
    if (getpid() != 1)
        return 99;
    int console = open("/dev/console", O_RDWR);
    if (console < 0)
        return 98;
    for (int i = 0; i < 3; i++)
        dup2(console, i);
    setvbuf(stdout, NULL, _IONBF, 0);
    int result = mount("proc", "/proc", "proc", 0, NULL);
    char command[2048] = {0};
    FILE *cmdline = fopen("/proc/cmdline", "r");
    if (!cmdline || !fgets(command, sizeof(command), cmdline) ||
        !strstr(command, "plb.cpu_probe=" PLB_CPU_NONCE))
        result = 1;
    if (cmdline)
        fclose(cmdline);
    struct utsname guest = {0};
    if (uname(&guest))
        result = 1;
    const size_t bytes = PLB_CPU_WORKLOAD_BYTES;
    volatile unsigned char *data = result ? NULL : malloc(bytes);
    if (!data)
        result = 1;
    if (data) {
        for (size_t i = 0; i < bytes; i++) {
            data[i] = (unsigned char)(i * 17 + i / 4096);
            if ((i + 1) % (64 * 1024 * 1024) == 0)
                printf("PLB_CPU_PROGRESS nonce=%s phase=write mib=%zu\n", PLB_CPU_NONCE,
                       (i + 1) / (1024 * 1024));
        }
        for (size_t i = 0; i < bytes; i++) {
            if (data[i] != (unsigned char)(i * 17 + i / 4096))
                result = 1;
            if ((i + 1) % (64 * 1024 * 1024) == 0)
                printf("PLB_CPU_PROGRESS nonce=%s phase=read mib=%zu\n", PLB_CPU_NONCE,
                       (i + 1) / (1024 * 1024));
        }
        free((void *)data);
    }
    int sysfs_ready = !result && !mount("sysfs", "/sys", "sysfs", 0, NULL);
#ifdef PLB_CPU_NULL_AUDIO
    int sound_card = 0;
    for (int attempt = 0; sysfs_ready && attempt < 40; attempt++) {
        if (access("/sys/class/sound/card0/id", R_OK) == 0) {
            sound_card = 1;
            break;
        }
        usleep(50000);
    }
    printf("PLB_CPU_NULL_AUDIO nonce=%s card_present=%d host_playback=0 host_capture=0\n",
           PLB_CPU_NONCE, sound_card);
    if (!sound_card)
        result = 1;
#endif
    printf("PLB_CPU_GUEST nonce=%s result=%d arch=%s release=%s bytes=%zu\n", PLB_CPU_NONCE, result,
           guest.machine, guest.release, bytes);
    if (!result && sysfs_ready && access("/sys/block/vda/dev", R_OK) == 0) {
        FILE *device = fopen("/sys/block/vda/dev", "r");
        unsigned major_number = 0, minor_number = 0;
        result = !device || fscanf(device, "%u:%u", &major_number, &minor_number) != 2;
        if (device)
            fclose(device);
        if (!result)
            result = mknod("/dev/vda", S_IFBLK | 0600, makedev(major_number, minor_number)) ||
                     mount("/dev/vda", "/newroot", "ext4", MS_RDONLY, "noload");
        if (!result) {
            pid_t child = fork();
            if (!child) {
                if (chroot("/newroot") || chdir("/"))
                    _exit(81);
                clearenv();
                setenv("LANG", "C", 1);
                execl("/usr/bin/dpkg", "dpkg", "--print-architecture", (char *)NULL);
                _exit(82);
            }
            int status = -1;
            result = child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
                     WEXITSTATUS(status);
        }
        printf("PLB_CPU_DEBIAN_READONLY result=%d\n", result);
    }
    printf("PLB_CPU_FINISHED nonce=%s result=%d\n", PLB_CPU_NONCE, result);
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}
