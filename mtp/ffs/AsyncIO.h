/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _ASYNCIO_H
#define _ASYNCIO_H

#include <fcntl.h>
#include <linux/aio_abi.h>
#include <memory>
#include <signal.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>
#include <thread>
#include <unistd.h>

/**
 * Provides a subset of POSIX aio operations, as well
 * as similar operations with splice and threadpools.
 */

struct aiocb {
	int aio_fildes;		// Assumed to be the source for splices
	void *aio_buf;		// Unused for splices

	// Used for threadpool operations only, freed automatically
	std::unique_ptr<char[]> aio_pool_buf;

	off_t aio_offset;
	size_t aio_nbytes;

	int aio_sink;		// Unused for non splice r/w

	// Used internally
	std::thread thread;
	ssize_t ret;
	int error;

	~aiocb();
};

// Submit a request for IO to be completed
int aio_read(struct aiocb *);
int aio_write(struct aiocb *);
int aio_splice_read(struct aiocb *);
int aio_splice_write(struct aiocb *);

// Suspend current thread until given IO is complete, at which point
// its return value and any errors can be accessed
// All submitted requests must have a corresponding suspend.
// aiocb->aio_buf must refer to valid memory until after the suspend call
int aio_suspend(struct aiocb *[], int, const struct timespec *);
int aio_error(const struct aiocb *);
ssize_t aio_return(struct aiocb *);

// (Currently unimplemented)
int aio_cancel(int, struct aiocb *);

// Initialize a threadpool to perform IO. Only one pool can be
// running at a time.
void aio_pool_write_init();
void aio_pool_splice_init();
// Suspend current thread until all queued work is complete, then ends the threadpool
void aio_pool_end();
// Submit IO work for the threadpool to complete. Memory associated with the work is
// freed automatically when the work is complete.
int aio_pool_write(struct aiocb *);

#endif // ASYNCIO_H

