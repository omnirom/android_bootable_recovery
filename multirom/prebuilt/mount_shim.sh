#!/sbin/sh
REAL_MOUNT="/sbin/busybox mount"
OPTS=""

next_are_opts=false

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

$REAL_MOUNT $OPTS
return $?
