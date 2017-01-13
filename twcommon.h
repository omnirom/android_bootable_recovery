#ifndef TWCOMMON_HPP
#define TWCOMMON_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include "twlogging/twlogging.h"

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

#ifdef __cplusplus
}
#endif

#endif  // TWCOMMON_HPP
