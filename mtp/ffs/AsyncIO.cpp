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

#include <android-base/logging.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "AsyncIO.h"

void read_func(struct aiocb *aiocbp) {
	aiocbp->ret = TEMP_FAILURE_RETRY(pread(aiocbp->aio_fildes,
				aiocbp->aio_buf, aiocbp->aio_nbytes, aiocbp->aio_offset));
	if (aiocbp->ret == -1) aiocbp->error = errno;
}

void write_func(struct aiocb *aiocbp) {
	aiocbp->ret = TEMP_FAILURE_RETRY(pwrite(aiocbp->aio_fildes,
				aiocbp->aio_buf, aiocbp->aio_nbytes, aiocbp->aio_offset));
	if (aiocbp->ret == -1) aiocbp->error = errno;
}

void splice_read_func(struct aiocb *aiocbp) {
	loff_t long_offset = aiocbp->aio_offset;
	aiocbp->ret = TEMP_FAILURE_RETRY(splice(aiocbp->aio_fildes,
				&long_offset, aiocbp->aio_sink,
				NULL, aiocbp->aio_nbytes, 0));
	if (aiocbp->ret == -1) aiocbp->error = errno;
}

void splice_write_func(struct aiocb *aiocbp) {
	loff_t long_offset = aiocbp->aio_offset;
	aiocbp->ret = TEMP_FAILURE_RETRY(splice(aiocbp->aio_fildes, NULL,
				aiocbp->aio_sink, &long_offset,
				aiocbp->aio_nbytes, 0));
	if (aiocbp->ret == -1) aiocbp->error = errno;
}

std::queue<std::unique_ptr<struct aiocb>> queue;
std::mutex queue_lock;
std::condition_variable queue_cond;
std::condition_variable write_cond;
int done = 1;
void splice_write_pool_func(int) {
	while(1) {
		std::unique_lock<std::mutex> lk(queue_lock);
		queue_cond.wait(lk, []{return !queue.empty() || done;});
		if (queue.empty() && done) {
			return;
		}
		std::unique_ptr<struct aiocb> aiocbp = std::move(queue.front());
		queue.pop();
		lk.unlock();
		write_cond.notify_one();
		splice_write_func(aiocbp.get());
		close(aiocbp->aio_fildes);
	}
}

void write_pool_func(int) {
	while(1) {
		std::unique_lock<std::mutex> lk(queue_lock);
		queue_cond.wait(lk, []{return !queue.empty() || done;});
		if (queue.empty() && done) {
			return;
		}
		std::unique_ptr<struct aiocb> aiocbp = std::move(queue.front());
		queue.pop();
		lk.unlock();
		write_cond.notify_one();
		aiocbp->ret = TEMP_FAILURE_RETRY(pwrite(aiocbp->aio_fildes,
					aiocbp->aio_pool_buf.get(), aiocbp->aio_nbytes, aiocbp->aio_offset));
		if (aiocbp->ret == -1) aiocbp->error = errno;
	}
}

constexpr int NUM_THREADS = 1;
constexpr int MAX_QUEUE_SIZE = 10;
std::thread pool[NUM_THREADS];

aiocb::~aiocb() {
	CHECK(!thread.joinable());
}

void aio_pool_init(void(f)(int)) {
	CHECK(done == 1);
	done = 0;
	for (int i = 0; i < NUM_THREADS; i++) {
		pool[i] = std::thread(f, i);
	}
}

void aio_pool_splice_init() {
	aio_pool_init(splice_write_pool_func);
}

void aio_pool_write_init() {
	aio_pool_init(write_pool_func);
}

void aio_pool_end() {
	done = 1;
	for (int i = 0; i < NUM_THREADS; i++) {
		std::unique_lock<std::mutex> lk(queue_lock);
		lk.unlock();
		queue_cond.notify_one();
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		pool[i].join();
	}
}

// used for both writes and splices depending on which init was used before.
int aio_pool_write(struct aiocb *aiocbp) {
	std::unique_lock<std::mutex> lk(queue_lock);
	write_cond.wait(lk, []{return queue.size() < MAX_QUEUE_SIZE;});
	queue.push(std::unique_ptr<struct aiocb>(aiocbp));
	lk.unlock();
	queue_cond.notify_one();
	return 0;
}

int aio_read(struct aiocb *aiocbp) {
	aiocbp->thread = std::thread(read_func, aiocbp);
	return 0;
}

int aio_write(struct aiocb *aiocbp) {
	aiocbp->thread = std::thread(write_func, aiocbp);
	return 0;
}

int aio_splice_read(struct aiocb *aiocbp) {
	aiocbp->thread = std::thread(splice_read_func, aiocbp);
	return 0;
}

int aio_splice_write(struct aiocb *aiocbp) {
	aiocbp->thread = std::thread(splice_write_func, aiocbp);
	return 0;
}

int aio_error(const struct aiocb *aiocbp) {
	return aiocbp->error;
}

ssize_t aio_return(struct aiocb *aiocbp) {
	return aiocbp->ret;
}

int aio_suspend(struct aiocb *aiocbp[], int n,
		const struct timespec *) {
	for (int i = 0; i < n; i++) {
		aiocbp[i]->thread.join();
	}
	return 0;
}

int aio_cancel(int, struct aiocb *) {
	// Not implemented
	return -1;
}

