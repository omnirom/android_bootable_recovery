#ifndef TWLOGGING_H
#define TWLOGGING_H

#include <stdio.h>
#ifdef ERROR_OUTPUT_GUI
#include "../gui/gui.h"
#endif

void print_time_prefix(FILE *std);

#ifdef DEBUG
#define LOGDBG(...) \
	do { \
		print_time_prefix(stdout); \
		printf("D:" __VA_ARGS__); \
		fflush(stdout); \
	} while (0)
#else
#define LOGDBG(...) do { } while (0)
#endif /* DEBUG */

#define LOGVERB(...) \
	do { \
		print_time_prefix(stdout); \
		printf("V:" __VA_ARGS__); \
		fflush(stdout); \
	} while (0)

#define LOGINFO(...) \
	do { \
		print_time_prefix(stdout); \
		printf("I:" __VA_ARGS__); \
		fflush(stdout); \
	} while (0)

#define LOGWARN(...) \
	do { \
		print_time_prefix(stdout); \
		printf("W:" __VA_ARGS__); \
		fflush(stdout); \
	} while (0)

#ifdef ERROR_OUTPUT_GUI
#define LOGERR(...) gui_print_color("error", "E: " __VA_ARGS__)
#else

#define LOGERR(...) \
	do { \
		print_time_prefix(stdout); \
		printf("E:" __VA_ARGS__); \
		fflush(stdout); \
	} while (0)

#define gui_print LOGINFO

#endif /* ERROR_OUTPUT_GUI */

/* short form aliases */
#define LOGD LOGDBG
#define LOGV LOGVERB
#define LOGI LOGINFO
#define LOGW LOGWARN
#define LOGE LOGERR

#endif /* TWLOGGING_H */
