#!/bin/bash

#设置电池的位置，将原来的 270 修改成400

#sed -i "s/<placement x=\"270\" y=\"76\" \/>/<placement x=\"400\" y=\"76\" \/>/" "$2"

#替换ui.xml中的变量
#将 <text>...</text> ; 修改成 <text id=....>...</text>
cat "$1" | while read line
 do
	orig=`echo "$line" | sed -ne '/ id=\"/ { s/\(<[^>]*\)\([ \t]\+id=\"[^\">]*\"\)\([^>]*>[^<>]*<\/.*>\)/\1\3/; s/\//\\\\\//g; s/\"/\\\"/g; s/\&/\\\&/g; p }'`
	if [ -n "$orig" ]
	then
		sed -i "s/$orig/`echo "$line" | sed -ne '/ id=\"/ {s/\//\\\\\//g; s/\"/\\\"/g; s/\&/\\\&/g; p }'`/g" "$2"
	fi
done


#将双语言功能插入
sed -i "s/<action function=\"reload\"><\/action>/<action function=\"page\">language_reload<\/action>/" "$2"


#添加界面 
#sed -i "s/^[ \t]*<\/pages>/\n\t\t<page name=\"language_reload\">\n\\t\t<object type=\"template\" name=\"header\" \/>\n\n\t\t<object type=\"text\" color=\"%text_color%\">\n\t\t\t<font resource=\"font\" \/>\n\t\t\t<placement x=\"%center_x%\" y=\"%row1_header_y%\" placement=\"5\" \/>\n\t\t\t<text id=\"language_select\">Select Language<\/text>\n\t\t<\/object>\n\n\\t\t\t<object type=\"checkbox\">\n\t\t\t\t<placement x=\"%col1_x%\" y=\"%row1_text_y%\" \/>\n\t\t\t\t<font resource=\"font\" color=\"%text_color%\" \/>\n\\t\t\t\t<text id=\"set_lang_en\">Set lang to En<\/text>\n\t\t\t\t<data variable=\"tw_lang_name_en\" \/>\n\t\t\t\t<image checked=\"checkbox_true\" unchecked=\"checkbox_false\" \/>\n\\t\t\t<\/object>\n\t\t\t<object type=\"checkbox\">\n\t\t\t\t<placement x=\"%col1_x%\" y=\"%row2_text_y%\" \/>\n\t\t\t\t<font resource=\"font\" color=\"%text_color%\" \/>\n\t\t\t\t<text id=\"set_lang_zh\">Set lang to zh-CN<\/text>\n\t\t\t\t<data variable=\"tw_lang_name_zh-CN\" \/>\n\t\t\t\t<image checked=\"checkbox_true\" unchecked=\"checkbox_false\" \/>\n\t\t\t<\/object>\n\n\t\t\t<object type=\"button\">\n\t\t\t\t<placement x=\"%col_center_x%\" y=\"%tz_set_y%\" \/>\n\t\t\t\t<font resource=\"font\" color=\"%button_text_color%\" \/>\n\t\t\t\t<text id=\"lang_set\">Set Language<\/text>\n\t\t\t\t<image resource=\"main_button\" \/>\n\t\t\t\t<actions>\n\t\t\t\t\t<action function=\"reload\"><\/action>\n\t\t\t\t<\/actions>\n\t\t\t\t<\/object>\n\n\t\t\t\t<object type=\"text\" color=\"%text_color%\">\n\t\t\t\t\t<font resource=\"font\" \/>\n\t\t\t\t\t<placement x=\"%center_x%\" y=\"%tz_current_y%\" placement=\"5\" \/>\n\t\t\t\t\<text id=\"language_current\">Current Language: %tw_lang_name%<\/text>\n\t\t\t<\/object>\n\\n\t\t\t<object type=\"action\">\n\t\t\t\t\t<touch key=\"home\" \/>\n\t\t\t\t\t<action function=\"page\">main<\/action>\n\t\t\t\<\/object>\n\t\t\t\t<object type=\"action\">\n\t\t\t\t\t<touch key=\"back\" \/>\n\t\t\t\t\t<action function=\"page\">advanced<\/action>\n\t\t\t\t<\/object>\n\n\t\t\t\t<object type=\"template\" name=\"footer\" \/>\n\t\t<\/page>\n\\t<\/pages>\n/" "$2"

sed -i "s/^[ \t]*<\/pages>/\n\t\t<page name=\"language_reload\">\n\
                        <object type=\"template\" name=\"header\" \/>\n\
\n\
			<object type=\"listbox\">\n\
				<highlight color=\"%fileselector_highlight_color%\" \/>\n\
                                <placement x=\"%listbox_x%\" y=\"%row1_header_y%\" w=\"%listbox_width%\" h=\"130\" \/>\n\
				<header background=\"%fileselector_header_background%\" textcolor=\"%fileselector_header_textcolor%\" separatorcolor=\"%fileselector_header_separatorcolor%\" separatorheight=\"%fileselector_header_separatorheight%\" \/>\n\
				<fastscroll linecolor=\"%fastscroll_linecolor%\" rectcolor=\"%fastscroll_rectcolor%\" w=\"%fastscroll_w%\" linew=\"%fastscroll_linew%\" rectw=\"%fastscroll_rectw%\" recth=\"%fastscroll_recth%\" \/>\n\
                                <text id=\"language_select\">Select Language<\/text>\n\
				<icon selected=\"radio_true\" unselected=\"radio_false\" \/>\n\
				<separator color=\"%fileselector_separatorcolor%\" height=\"%fileselector_separatorheight%\" \/>\n\
				<background color=\"%listbox_background%\" \/>\n\
				<font resource=\"font\" spacing=\"%listbox_spacing%\" color=\"%text_color%\" highlightcolor=\"%fileselector_highlight_font_color%\" \/>\n\
\n\
				<data name=\"tw_lang_guisel\" \/>\n\
				<listitem id=\"lang_en\" name=\"English\">en<\/listitem>\n\
				<listitem id=\"lang_zh-CN\" name=\"Chinese\">zh-CN<\/listitem>\n\
			<\/object>\n\
\n\
			<object type=\"button\">\n\
				<highlight color=\"%highlight_color%\" \/>\n\
				<placement x=\"%col_center_x%\" y=\"%tz_set_y%\" \/>\n\
				<font resource=\"font\" color=\"%button_text_color%\" \/>\n\
                                <text id=\"lang_set\">Set Language<\/text>\n\
				<image resource=\"main_button\" \/>\n\
				<action function=\"reload\"><\/action>\n\
			<\/object>\n\
\n\
			<object type=\"text\" color=\"%text_color%\">\n\
				<font resource=\"font\" \/>\n\
				<placement x=\"%center_x%\" y=\"%tz_current_y%\" placement=\"5\" \/>\n\
				<text id=\"language_current\">Current Language: %tw_lang_name%<\/text>\n\
			<\/object>\n\
\n\
			<object type=\"action\">\n\
				<touch key=\"home\" \/>\n\
				<action function=\"page\">main<\/action>\n\
			<\/object>\n\
\n\
			<object type=\"action\">\n\
				<touch key=\"back\" \/>\n\
                                <action function=\"page\">advanced<\/action>\n\
			<\/object>\n\
\n\
			<object type=\"template\" name=\"footer\" \/>\n\
		<\/page>\n\
         <\/pages>\n/" "$2"

