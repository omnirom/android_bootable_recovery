/* vi: set sw=4 ts=4: */
/*
 * Stub for linking busybox binary against libbusybox.
 *
 * Copyright (C) 2007 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#if ENABLE_BUILD_LIBBUSYBOX
#include "busybox.h"
int main(int argc UNUSED_PARAM, char **argv)
{
	return lbb_main(argv);
}
#endif
