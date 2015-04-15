#!/sbin/sh

{
	{
		/sbin/e2fsck -C2 "$@" 3>&1 1>&2 2>&3 3>&-
		echo EXIT $?
	} | {
		/sbin/awk '
		/^EXIT / { exit $2; }
		{ print "set_progress " (($1 - 1) / 5 + ($2 / $3 / 5)); }
		'
	}
} 2>&1

