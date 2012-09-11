#ifndef _GUI_HEADER
#define _GUI_HEADER

int gui_console_only();
int gui_init();
int gui_loadResources();
int gui_start();
void gui_print(const char *fmt, ...);
void gui_print_overwrite(const char *fmt, ...);

#endif  // _GUI_HEADER

