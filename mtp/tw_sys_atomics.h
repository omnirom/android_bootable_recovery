/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _TW_SYS_ATOMICS_H
#define _TW_SYS_ATOMICS_H

#include <sys/cdefs.h>
#include <sys/time.h>

__BEGIN_DECLS

/* Note: atomic operations that were exported by the C library didn't
 *       provide any memory barriers, which created potential issues on
 *       multi-core devices. We now define them as inlined calls to
 *       GCC sync builtins, which always provide a full barrier.
 *
 *       NOTE: The C library still exports atomic functions by the same
 *              name to ensure ABI stability for existing NDK machine code.
 *
 *       If you are an NDK developer, we encourage you to rebuild your
 *       unmodified sources against this header as soon as possible.
 */

/* This was kanged from Android 4.4 bionic/libc/include/sys/atomics.h
 * This header was removed in Android 5.0 in favor of stdatomics.h but
 * to maintain compatibility across multiple trees, we are including our
 * own copy.
 */

#ifndef __ATOMIC_INLINE__
#define __ATOMIC_INLINE__ static __inline__ __attribute__((always_inline))
#endif

__ATOMIC_INLINE__ int
__tw_atomic_cmpxchg(int old_value, int new_value, volatile int* ptr)
{
    /* We must return 0 on success */
    return __sync_val_compare_and_swap(ptr, old_value, new_value) != old_value;
}

__END_DECLS

#endif /* _TW_SYS_ATOMICS_H */
