#!/bin/bash

#修改支持中文字体

#sed -i "s/<placement x=\"270\" y=\"76\" \/>/<placement x=\"400\" y=\"76\" \/>/" "$2"
sed -i "s/<resource name=\"font\" type=\"font\" filename=\"RobotoCondensed-Regular.ttf\" size=\"30\" fallback=\"Roboto-Condensed-30\" \/>/<resource name=\"font\" type=\"font\" filename=\"RuHeiZhong.ttf\" size=\"28\" \/>/" "$1" 

