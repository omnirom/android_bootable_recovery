#ifndef TWCOMMON_HPP
#define TWCOMMON_HPP

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUILD_TWRPTAR_MAIN
#include "gui/gui.h"
#define LOGERR(...) gui_print("E:" __VA_ARGS__)
#define LOGINFO(...) fprintf(stdout, "I:" __VA_ARGS__)
#else
#define LOGERR(...) printf("E:" __VA_ARGS__)
#define LOGINFO(...) printf("I:" __VA_ARGS__)
#define gui_print(...) printf( __VA_ARGS__ )
#endif

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

#ifdef __cplusplus
}
#endif

#endif  // TWCOMMON_HPP
