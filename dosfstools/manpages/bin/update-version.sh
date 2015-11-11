#!/bin/sh

## dosfstools(7)
## Copyright (C) 2006-2014 Daniel Baumann <mail@daniel-baumann.ch>
##
## This program comes with ABSOLUTELY NO WARRANTY; for details see COPYING.
## This is free software, and you are welcome to redistribute it
## under certain conditions; see COPYING for details.


set -e

PROJECT="dosfstools"
VERSION="$(cat ../VERSION)"

DATE="$(LC_ALL=C date +%Y\\\\-%m\\\\-%d)"

DAY="$(LC_ALL=C date +%d)"
MONTH="$(LC_ALL=C date +%m)"
YEAR="$(LC_ALL=C date +%Y)"

echo "Updating version headers..."

for MANPAGE in en/*
do
	PROGRAM="$(basename ${MANPAGE} | sed -e 's|\(.*\).[0-9]$|\1|' | tr [a-z] [A-Z])"
	SECTION="$(basename ${MANPAGE} | sed -e 's|.*.\([0-9]\)$|\1|')"

	sed -i -e "s|^.TH.*$|.TH ${PROGRAM} ${SECTION} ${DATE} ${VERSION} \"${PROJECT}\"|" ${MANPAGE}
done

# European date format
for _LANGUAGE in de es fr it
do
	if ls po/${_LANGUAGE}/*.po > /dev/null 2>&1
	then
		for _FILE in po/${_LANGUAGE}/*.po
		do
			sed -i  -e "s|^msgstr .*.2014-.*$|msgstr \"${DAY}.${MONTH}.${YEAR}\"|g" \
				-e "s|^msgstr .*.2014\"$|msgstr \"${DAY}.${MONTH}.${YEAR}\"|g" \
			"${_FILE}"
		done
	fi
done

# Brazilian date format
if ls po/pt_BR/*.po > /dev/null 2>&1
then
	for _FILE in po/pt_BR/*.po
	do
		sed -i  -e "s|^msgstr .*.2014-.*$|msgstr \"${DAY}-${MONTH}-${YEAR}\"|g" \
			-e "s|^msgstr .*-2014\"$|msgstr \"${DAY}-${MONTH}-${YEAR}\"|g" \
		"${_FILE}"
	done
fi
