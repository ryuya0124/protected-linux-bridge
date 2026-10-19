#!/system/bin/sh
# Guest-only experiment. The source-built runner substitutes one fixed directory.
set -eu
[ "$(id -u)" = 0 ]
[ "$(getprop ro.product.device)" = vsoc_arm64_only ]
[ "$(getprop ro.hardware)" = cutf_cvm ]
[ "$(getprop ro.build.version.incremental)" = 16373615 ]
[ -z "$(ip route show table main default)" ]
! ip link show plb0 >/dev/null 2>&1
cd "@DIRECTORY@"
children=
remember() {
    born="$(awk '{print $22}' /proc/$1/stat)"
    children="$children $1:$born"
}
printf '%s %s %s\n' "$$" "$(awk '{print $22}' /proc/$$/stat)" \
    "$(cat /proc/sys/kernel/random/boot_id)" >supervisor.pid
cleanup() {
    trap - EXIT HUP INT TERM
    for entry in $children; do
        child=${entry%:*}
        born=${entry#*:}
        now="$(awk '{print $22}' /proc/$child/stat 2>/dev/null || true)"
        if [ -n "$born" ] && [ "$now" = "$born" ]; then kill "$child" 2>/dev/null || true; fi
        wait "$child" 2>/dev/null || true
    done
    rm -f ready supervisor.pid
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
./loopback-relay >loopback.log 2>&1 &
remember "$!"
./hev-socks5-tunnel tunnel.yml >tunnel.log 2>&1 &
remember "$!"
i=0
until ip link show plb0 >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -lt 50 ]
    sleep .1
done
ip link set plb0 up
CLASSPATH="$PWD/agent.jar" app_process -Xmx32m /system/bin \
    net.ryuya_dev.protectedlinux.guestnetwork.GuestNetworkAgent >agent.log 2>&1 &
agent=$!
remember "$agent"
printf '%s\n' "$children" >ready
wait "$agent"
