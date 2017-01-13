#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>

#include "twlogging.h"

void print_time_prefix(FILE *std)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	fprintf(std, "[%5lu.%06ld] ",
		(unsigned long)now.tv_sec, now.tv_nsec / 1000);
}

#ifdef __cplusplus
}
#endif
