/*
 * Copyright (C) 2015 The Team Win Recovery Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pthread.h>
#include <stdio.h>
#include "tw_atomic.hpp"

/*
 * According to this documentation:
 * https://developer.android.com/training/articles/smp.html
 * it is recommended to use mutexes instead of atomics. This class
 * provides us with a wrapper to make "atomic" variables easy to use.
 */

TWAtomicInt::TWAtomicInt(int initial_value /* = 0 */) {
	if (pthread_mutex_init(&mutex_lock, NULL) != 0) {
		// This should hopefully never happen. If it does, the
		// operations will not be atomic, but we will allow things to
		// continue anyway after logging the issue and just hope for
		// the best.
		printf("TWAtomic error initializing mutex.\n");
		use_mutex = false;
	} else {
		use_mutex = true;
	}
	value = initial_value;
}

TWAtomicInt::~TWAtomicInt() {
	if (use_mutex)
		pthread_mutex_destroy(&mutex_lock);
}

void TWAtomicInt::set_value(int new_value) {
	if (use_mutex) {
		pthread_mutex_lock(&mutex_lock);
		value = new_value;
		pthread_mutex_unlock(&mutex_lock);
	} else {
		value = new_value;
	}
}

int TWAtomicInt::get_value(void) {
	int ret_val;

	if (use_mutex) {
		pthread_mutex_lock(&mutex_lock);
		ret_val = value;
		pthread_mutex_unlock(&mutex_lock);
	} else {
		ret_val = value;
	}
	return ret_val;
}
