#!/sbin/sh
REAL_MOUNT="/sbin/busybox mount"
OPTS=""

next_are_opts=false
allow_mount=true

remove_ro() {
    OIFS=$IFS
    IFS=','

    res=""
    for opt in $1; do
        if [ "$opt" = "ro" ]; then
            opt="rw"
        fi

        if [ -z "$res" ]; then
            res="$opt"
        else
            res="${res},${opt}"
        fi
    done

    IFS=$OIFS
    echo $res
}

for arg in "$@"; do
    case $arg in
        /system | /data | /cache)
            allow_mount=false
            ;;
        -r)
            OPTS="${OPTS} -w"
            ;;
        -o)
            next_are_opts=true
            OPTS="${OPTS} $arg"
            ;;
        *)
            if $next_are_opts; then
                next_are_opts=false
                arg="$(remove_ro $arg)"
            fi
            OPTS="${OPTS} $arg"
            ;;
    esac
done

if $allow_mount; then
    $REAL_MOUNT $OPTS
    return $?
else
    return 0
fi
