/* MIT - Copyright (c) 2026 ryuya0124. Runs only inside the protected guest VM. */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <netinet/in.h>
#include <net/if.h>
#include <termios.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "stream_relay.h"
#ifndef AF_VSOCK
#define AF_VSOCK 40
#endif
#define AGENT_PORT 50442
#define PROXY_PORT 50441
struct sockaddr_vm {
    sa_family_t svm_family;
    unsigned short reserved;
    unsigned int svm_port, svm_cid;
    unsigned char zero[4];
};
static volatile sig_atomic_t quitting;
static int poweroff_requested;
static void stop_guest(int signum) {
    (void)signum;
    quitting = 1;
}
static int vsock_connect(unsigned port) {
    int fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_vm a = {.svm_family = AF_VSOCK, .svm_port = port, .svm_cid = 2};
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    int result = connect(fd, (struct sockaddr *)&a, sizeof(a));
    if (result && errno == EINPROGRESS) {
        struct pollfd p = {fd, POLLOUT, 0};
        int error = 1;
        socklen_t size = sizeof(error);
        if (poll(&p, 1, 5000) > 0 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error)
            result = 0;
    }
    if (result || fcntl(fd, F_SETFL, flags) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
static int guest_lifetime = 1800;
#include "guest_network.h"
#include "gpu_guest.h"
static int local_connect(unsigned port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in a = {.sin_family = AF_INET,
                            .sin_port = htons((unsigned short)port),
                            .sin_addr = {htonl(INADDR_LOOPBACK)}};
    if (connect(fd, (struct sockaddr *)&a, sizeof(a))) {
        close(fd);
        return -1;
    }
    return fd;
}
static int local_listener(unsigned port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {.sin_family = AF_INET,
                            .sin_port = htons((unsigned short)port),
                            .sin_addr = {htonl(INADDR_LOOPBACK)}};
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) || listen(fd, 16)) {
        close(fd);
        return -1;
    }
    return fd;
}
static void loopback_up(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq iface = {0};
    strcpy(iface.ifr_name, "lo");
    if (fd >= 0) {
        if (!ioctl(fd, SIOCGIFFLAGS, &iface)) {
            iface.ifr_flags |= IFF_UP;
            ioctl(fd, SIOCSIFFLAGS, &iface);
        }
        close(fd);
    }
}
static void proxy_forwarder(unsigned port) {
    int listener = local_listener(port);
    if (listener < 0)
        _exit(40);
    signal(SIGCHLD, SIG_DFL);
    int active = 0;
    for (;;) {
        while (waitpid(-1, NULL, WNOHANG) > 0)
            if (active > 0)
                active--;
        struct pollfd p = {listener, POLLIN, 0};
        if (poll(&p, 1, 200) <= 0)
            continue;
        int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        if (active >= 16) {
            close(client);
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
            close(listener);
            alarm(15);
            int remote = vsock_connect(PROXY_PORT);
            if (remote >= 0) {
                alarm(0);
                plb_relay(client, remote, guest_lifetime, 0);
                close(remote);
            }
            close(client);
            _exit(0);
        }
        if (pid > 0)
            active++;
        close(client);
    }
}
static void configure_environment(void) {
    char width[12], height[12];
    snprintf(width, sizeof(width), "%d", desktop_width);
    snprintf(height, sizeof(height), "%d", desktop_height);
    setenv("PLB_DISPLAY_WIDTH", width, 1);
    setenv("PLB_DISPLAY_HEIGHT", height, 1);
    setenv("PULSE_SERVER", "unix:/run/plb/audio/native", 1);
    setenv("ALSA_CONFIG_PATH", "/run/plb/audio/asound.conf", 1);
    setenv("PATH", "/run/plb:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("TERM", "xterm-256color", 1);
    setenv("SHELL", "/bin/bash", 1);
    setenv("HOME", "/root", 1);
    setenv("LANG", "C.UTF-8", 1);
    setenv("COLUMNS", "80", 1);
    setenv("LINES", "24", 1);
    setenv("DISPLAY", ":1", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    if (!access("/root/.config/protected-linux/gpu-enabled", F_OK)) {
        setenv("GALLIUM_DRIVER", "virpipe", 1);
        setenv("VTEST_SOCKET_NAME", PLB_GUEST_GPU_SOCKET, 1);
    }
    for (int i = 0; i < 3; i++)
        setenv((const char *[]){"http_proxy", "https_proxy", "ALL_PROXY"}[i],
               "http://127.0.0.1:3128", 1);
    setenv("no_proxy", "localhost,127.0.0.1,::1", 1);
    setenv("PS1", "\\u@\\h:\\w\\$ ", 1);
}
static void terminal_session(int client) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0 || grantpt(master) || unlockpt(master)) {
        plb_write_all(client, "\3", 1);
        _exit(41);
    }
    char name[128];
    if (ptsname_r(master, name, sizeof(name))) {
        plb_write_all(client, "\3", 1);
        _exit(42);
    }
    struct winsize size = {.ws_row = 24, .ws_col = 80};
    ioctl(master, TIOCSWINSZ, &size);
    pid_t shell = fork();
    if (shell == 0) {
        setsid();
        int tty = open(name, O_RDWR);
        if (tty < 0)
            _exit(43);
        ioctl(tty, TIOCSCTTY, 0);
        for (int i = 0; i < 3; i++)
            dup2(tty, i);
        if (tty > 2)
            close(tty);
        close(master);
        close(client);
        configure_environment();
        chdir("/root");
        execl("/bin/bash", "bash", "--noprofile", "--norc", "-i", (char *)NULL);
        _exit(44);
    }
    if (shell < 0) {
        plb_write_all(client, "\3", 1);
        _exit(45);
    }
    plb_write_all(client, "\0", 1);
    plb_relay(client, master, guest_lifetime, 1);
    kill(-shell, SIGHUP);
    kill(shell, SIGTERM);
    close(master);
    close(client);
    for (int i = 0; i < 20; i++) {
        if (waitpid(shell, NULL, WNOHANG) != 0)
            _exit(0);
        usleep(100000);
    }
    kill(-shell, SIGKILL);
    kill(shell, SIGKILL);
    waitpid(shell, NULL, 0);
    _exit(0);
}
#include "guest_clipboard.h"
static int start_gui(void) {
    int test = local_connect(5901);
    if (test >= 0) {
        clipboard_track_start();
        return test;
    }
    if (access("/usr/bin/Xtigervnc", X_OK))
        return -2;
    pid_t server = fork();
    if (server == 0) {
        int log = open("/tmp/plb-vnc.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (log >= 0) {
            dup2(log, 1);
            dup2(log, 2);
            close(log);
        }
        configure_environment();
        char geometry[32], fps[12];
        snprintf(geometry, sizeof(geometry), "%dx%d", desktop_width, desktop_height);
        snprintf(fps, sizeof(fps), "%d", desktop_fps);
        unsetenv("LD_PRELOAD");
        execl("/usr/bin/Xtigervnc", "Xtigervnc", ":1", "-geometry", geometry, "-FrameRate", fps,
              "-depth", "24", "-localhost", "yes", "-SecurityTypes", "None", "-s", "0", "-nolisten",
              "tcp", "-rfbport", "5901", (char *)NULL);
        _exit(46);
    }
    if (server < 0)
        return -1;
    for (int i = 0; i < 80; i++) {
        test = local_connect(5901);
        if (test >= 0)
            break;
        usleep(100000);
    }
    if (test < 0) {
        kill(server, SIGTERM);
        return -1;
    }
    clipboard_track_start();
    if (fork() == 0) {
        configure_environment();
        execl("/bin/sh", "sh", "-c",
              "openbox >/tmp/plb-openbox.log 2>&1 & if test -f "
              "/root/.config/protected-linux/gpu-enabled && test -f "
              "/root/.config/protected-linux/desktop-gpu && command -v weston >/dev/null 2>&1; "
              "then weston --backend=x11 --renderer=gl --fullscreen --width=\"$PLB_DISPLAY_WIDTH\" "
              "--height=\"$PLB_DISPLAY_HEIGHT\" --socket=wayland-plb --idle-time=0 --no-config "
              "--xwayland --log=/tmp/plb-weston.log & compositor=$!; for i in $(seq 1 50); do test "
              "-S /run/user/0/wayland-plb && break; kill -0 \"$compositor\" 2>/dev/null || break; "
              "sleep .1; done; if test -S /run/user/0/wayland-plb; then "
              "WAYLAND_DISPLAY=wayland-plb weston-terminal >/tmp/plb-wayland-terminal.log 2>&1 & "
              "wait \"$compositor\"; fi; fi; xterm & terminal=$!; sleep 0.5; if command -v "
              "xsetroot >/dev/null 2>&1; then xsetroot -solid '#1d1b20'; fi; wait \"$terminal\"",
              (char *)NULL);
        _exit(47);
    }
    return test;
}

#include "guest_systemd.h"
#include "guest_audio.h"
#include "guest_migration.h"
#include "guest_storage.h"
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--storage-offline")) {
        if (getpid() != 1 || guest_boot_option(" plb.storage=") != 1)
            return 22;
        storage_guest_offline();
    }
    if (argc == 2 && !strcmp(argv[1], "--copy-disk")) {
        if (getpid() != 1 || guest_boot_option(" plb.migrate=") != 1)
            return 22;
        guest_migrate_disk();
    }
    int lifetime = guest_boot_option(" plb.lifetime=");
    /* A service/helper invocation must belong to this VM, not Android's root shell. */
    if (getpid() != 1 && lifetime <= 0)
        return 22;
    if (lifetime > 0)
        guest_lifetime = lifetime;
    int managed = argc == 2 && !strcmp(argv[1], "--serve");
    if (getpid() != 1 && !managed) {
        int gpu = gpu_command(argc, argv);
        return gpu >= 0 ? gpu : plb_network_command(argc, argv);
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, stop_guest);
    signal(SIGINT, stop_guest);
    loopback_up();
    if (getpid() == 1) {
        guest_protect_kernel_packages();
        guest_grow_filesystem();
        guest_start_systemd();
    }
    mkdir("/run/user", 0755);
    mkdir("/run/user/0", 0700);
    if (gpu_boot_settings())
        fputs("GRAPHICS_SETTINGS_FAILED\n", stderr);
    configure_environment();
    start_audio();
    if (fork() == 0) {
        proxy_forwarder(3128);
        _exit(0);
    }
    if (guest_boot_option(" plb.fullkernel=") && guest_boot_option(" plb.network=")) {
        if (fork() == 0) {
            proxy_forwarder(1080);
            _exit(0);
        }
        pid_t network = fork();
        if (network == 0) {
            int log = open("/tmp/plb-network.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (log >= 0) {
                dup2(log, 1);
                dup2(log, 2);
                close(log);
            }
            execl("/bin/sh", "sh", "/run/plb/plb-network", (char *)NULL);
            _exit(48);
        }
        // Don't hand a new shell/GUI to applications before guest routes/DNS are ready.
        if (network > 0)
            for (int i = 0; i < 100; i++) {
                if (!access("/run/plb/network/ready", F_OK) || waitpid(network, NULL, WNOHANG) != 0)
                    break;
                usleep(100000);
            }
    }
    if (fork() == 0) {
        gpu_forwarder();
        _exit(0);
    }
    int listener = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_vm a = {.svm_family = AF_VSOCK, .svm_port = AGENT_PORT, .svm_cid = 0xffffffff};
    if (listener < 0 || bind(listener, (struct sockaddr *)&a, sizeof(a)) || listen(listener, 16)) {
        perror("guest agent vsock");
        quitting = 1;
    }
    if (!quitting) {
        puts("PLB_GUEST_AGENT_READY");
        fflush(stdout);
    }
    int active = 0;
    while (!quitting) {
        while (waitpid(-1, NULL, WNOHANG) > 0)
            if (active > 0)
                active--;
        struct pollfd p = {listener, POLLIN, 0};
        if (poll(&p, 1, 200) <= 0)
            continue;
        struct sockaddr_vm peer;
        socklen_t size = sizeof(peer);
        int client = accept4(listener, (struct sockaddr *)&peer, &size, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        if (peer.svm_cid != 2 || active >= 16) {
            close(client);
            continue;
        }
        struct timeval t = {2, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
        char request = 0;
        ssize_t n = read(client, &request, 1);
        if (n == 1 && request == 'S') {
            guest_storage_usage(client);
            close(client);
            continue;
        }
        if (n == 1 && request == 'T') {
            guest_storage_status(client);
            close(client);
            continue;
        }
        if (n == 1 && request == 'N') {
            unsigned char status = !guest_boot_option(" plb.network=")      ? 1
                                   : access("/run/plb/network/ready", F_OK) ? 2
                                                                            : 3;
            plb_write_all(client, &status, 1);
            close(client);
            continue;
        }
        if (n == 1 && request == 'Q') {
            sync();
            plb_write_all(client, "\0", 1);
            close(client);
            poweroff_requested = 1;
            quitting = 1;
            break;
        }
        if (n != 1 || (request != 'P' && request != 'G' && request != 'C' && request != 'O' &&
                       request != 'I' && request != 'W')) {
            close(client);
            continue;
        }
        t.tv_sec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
        pid_t child = fork();
        if (child == 0) {
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            close(listener);
            if (request == 'P')
                terminal_session(client);
            else if (request == 'W') {
                clipboard_watch(client);
                close(client);
                _exit(0);
            } else if (request == 'O' || request == 'I') {
                audio_session(client, request == 'I');
                close(client);
                _exit(0);
            } else if (request == 'C') {
                plb_write_all(client, "\0", 1);
                clipboard_session(client);
                close(client);
                _exit(0);
            } else {
                int display = start_gui();
                char status = display == -2 ? 2 : display < 0 ? 3 : 0;
                plb_write_all(client, &status, 1);
                if (display >= 0) {
                    plb_relay(client, display, guest_lifetime, 0);
                    close(display);
                }
                close(client);
                _exit(0);
            }
        }
        if (child > 0)
            active++;
        close(client);
    }
    close(listener);
    if (managed && !poweroff_requested)
        _exit(0);
    if (managed) {
        execl("/bin/systemctl", "systemctl", "poweroff", "--no-block", (char *)NULL);
        execl("/usr/bin/systemctl", "systemctl", "poweroff", "--no-block", (char *)NULL);
        _exit(0);
    }
    kill(-1, SIGTERM);
    usleep(200000);
    sync();
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}
