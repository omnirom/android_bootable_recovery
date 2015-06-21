#!/bin/bash

#修改支持中文字体

#sed -i "s/<placement x=\"270\" y=\"76\" \/>/<placement x=\"400\" y=\"76\" \/>/" "$2"
sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"30\" fallback=\"Roboto-Condensed-30\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"28\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"30\" fallback=\"Roboto-Regular-30\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"30\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"40\" fallback=\"Roboto-Condensed-40\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"40\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"12\" fallback=\"Roboto-Condensed-12\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"10\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"14\" fallback=\"Roboto-Condensed-14\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"14\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"16\" fallback=\"Roboto-Condensed-16\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"16\" \/>/" "$1"

sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"20\" fallback=\"Roboto-Regular-20\" \/>/<resource name=\"font\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"20\" \/>/" "$1"

sed -i "s/<resource name=\"mediumfont\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"20\" fallback=\"Roboto-Regular-20\" \/>/<resource name=\"mediumfont\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"20\" \/>/" "$1"

sed -i "s/<resource name=\"filelist\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"20\" fallback=\"Roboto-Regular-20\" \/>/<resource name=\"filelist\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"20\" \/>/" "$1"

sed -i "s/<resource name=\"mediumfont\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"14\" fallback=\"Roboto-Condensed-14\" \/>/<resource name=\"mediumfont\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"14\" \/>/" "$1"

sed -i "s/<resource name=\"filelist\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"14\" fallback=\"Roboto-Condensed-14\" \/>/<resource name=\"filelist\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"14\" \/>/" "$1"

sed -i "s/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansMono.ttf\" size=\"12\" \/>/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"12\" \/>/" "$1"

sed -i "s/<resource name=\"mediumfont\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"40\" fallback=\"Roboto-Condensed-40\" \/>/<resource name=\"mediumfont\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"40\" \/>/" "$1"
sed -i "s/<resource name=\"filelist\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"40\" fallback=\"Roboto-Condensed-40\" \/>/<resource name=\"filelist\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"40\" \/>/" "$1"
sed -i "s/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansMono.ttf\" size=\"40\" \/>/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"40\" \/>/" "$1"
sed -i "s/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansMono.ttf\" size=\"30\" \/>/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"40\" \/>/" "$1"
sed -i "s/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansMono.ttf\" size=\"22\" \/>/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"22\" \/>/" "$1"
sed -i "s/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansMono.ttf\" size=\"16\" \/>/<resource name=\"fixed\" type=\"font\" filename=\"DroidSansFallback.ttf\" size=\"16\" \/>/" "$1"


