#!/bin/sh

# Note: was using sed OPTS CMD -- FILES
# but users complain that many sed implementations
# are misinterpreting --.

test $# -ge 2 || { echo "Syntax: $0 SRCTREE OBJTREE"; exit 1; }

# cd to objtree
cd -- "$2" || { echo "Syntax: $0 SRCTREE OBJTREE"; exit 1; }
# In separate objtree build, include/ might not exist yet
mkdir include 2>/dev/null

srctree="$1"

status() { printf '  %-8s%s\n' "$1" "$2"; }
gen() { status "GEN" "$@"; }
chk() { status "CHK" "$@"; }

# On OSX the sed implementation is not compatible with some of the
# features in this script, so this uses gsed and warns the user if
# it does not exist.
UNAME=$(uname -sm)
case "$UNAME" in
*Darwin*|*Macintosh*)
	SED_IMPL=$(which gsed)
	if [ $? != 0 ]; then
		echo "GNU sed is required for Darwin builds, please install and add 'gsed' to the path"
		exit 1;
	fi
	;;
*)
	SED_IMPL=sed
esac

generate()
{
	# NB: data to be inserted at INSERT line is coming on stdin
	local src="$1" dst="$2" header="$3"
	#chk "${dst}"
	{
		# Need to use printf: different shells have inconsistent
		# rules re handling of "\n" in echo params.
		printf "%s\n" "${header}"
		# print everything up to INSERT line
		$SED_IMPL -n '/^INSERT$/ q; p' "${src}"
		# copy stdin to stdout
		cat
		# print everything after INSERT line
		$SED_IMPL -n '/^INSERT$/ { :l; n; p; bl }' "${src}"
	} >"${dst}.tmp"
	if ! cmp -s "${dst}" "${dst}.tmp"; then
		gen "${dst}"
		mv "${dst}.tmp" "${dst}"
	else
		rm -f "${dst}.tmp"
	fi
}

# (Re)generate include/applets.h
$SED_IMPL -n 's@^//applet:@@p' "$srctree"/*/*.c "$srctree"/*/*/*.c \
| generate \
	"$srctree/include/applets.src.h" \
	"include/applets.h" \
	"/* DO NOT EDIT. This file is generated from applets.src.h */"

# (Re)generate include/usage.h
# We add line continuation backslash after each line,
# and insert empty line before each line which doesn't start
# with space or tab
$SED_IMPL -n -e 's@^//usage:\([ \t].*\)$@\1 \\@p' -e 's@^//usage:\([^ \t].*\)$@\n\1 \\@p' \
	"$srctree"/*/*.c "$srctree"/*/*/*.c \
| generate \
	"$srctree/include/usage.src.h" \
	"include/usage.h" \
	"/* DO NOT EDIT. This file is generated from usage.src.h */"

# (Re)generate */Kbuild and */Config.in
# We skip .dotdirs - makes git/svn/etc users happier
{ cd -- "$srctree" && find . -type d -not '(' -name '.?*' -prune ')'; } \
| while read -r d; do
	d="${d#./}"

	src="$srctree/$d/Kbuild.src"
	dst="$d/Kbuild"
	if test -f "$src"; then
		mkdir -p -- "$d" 2>/dev/null

		$SED_IMPL -n 's@^//kbuild:@@p' "$srctree/$d"/*.c \
		| generate \
			"${src}" "${dst}" \
			"# DO NOT EDIT. This file is generated from Kbuild.src"
	fi

	src="$srctree/$d/Config.src"
	dst="$d/Config.in"
	if test -f "$src"; then
		mkdir -p -- "$d" 2>/dev/null

		$SED_IMPL -n 's@^//config:@@p' "$srctree/$d"/*.c \
		| generate \
			"${src}" "${dst}" \
			"# DO NOT EDIT. This file is generated from Config.src"
	fi
done

# Last read failed. This is normal. Don't exit with its error code:
exit 0
