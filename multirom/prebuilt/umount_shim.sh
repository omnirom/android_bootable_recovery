#!/sbin/sh
REAL_UMOUNT="/sbin/busybox umount"
OPTS=""

allow_umount=true

for arg in "$@"; do
    case $arg in
        /system | /data | /cache)
            allow_umount=false
            ;;
        *)
            OPTS="${OPTS} $arg"
            ;;
    esac
done

if $allow_umount; then
    $REAL_UMOUNT $OPTS
    return $?
else
    return 0
fi
