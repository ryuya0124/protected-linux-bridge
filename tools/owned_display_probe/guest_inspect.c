/* MIT. Fixed read-only metadata query to the pinned owned guest; no physical media. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static volatile int stopping;
#include "android_guest_adb.h"
int main(void) {
    if (getuid())
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    size_t length = 0;
    const char identity[] =
        "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
        "getprop ro.build.version.incremental; getprop ro.build.version.sdk; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n37\n0\n";
    if (!output || android_adb(identity, output, &length) || length != sizeof(expected) - 1 ||
        memcmp(output, expected, length))
        return 21;
    const char inspect[] =
        "exec:sh -c 'echo PLB_GUEST_PERIPHERAL_INSPECT_BEGIN; "
        "echo BOOT=$(getprop sys.boot_completed); cat /proc/asound/cards; "
        "cat /proc/asound/pcm; ls /dev/snd; "
        "echo AUDIO_IGNORE_OUTPUT=$(getprop ro.boot.audio.tinyalsa.ignore_output); "
        "echo VIRTUAL_CAMERA_STATE=$(getprop init.svc.virtual_camera); "
        "service check virtual_camera; "
        "ls /system/bin/virtual_camera /system/etc/init/virtual_camera.hal.rc; "
        "ls /vendor/lib64/hw/audio* /vendor/bin/hw/*audio* /vendor/bin/hw/*camera*; "
        "getprop ro.hardware.audio.primary; getprop ro.hardware.camera; "
        "service list | grep -E \"audio|camera|sensor\"; "
        "dumpsys media.audio_policy | head -100; "
        "dumpsys media.camera | head -55; dumpsys sensorservice | head -55; "
        "echo PLB_GUEST_PERIPHERAL_INSPECT_COMPLETE'";
    int failed = android_adb(inspect, output, &length);
    if (!memmem(output, length, "PLB_GUEST_PERIPHERAL_INSPECT_COMPLETE", 37))
        failed = 1;
    fwrite(output, 1, length, stdout);
    free(output);
    return failed ? 22 : 0;
}
