/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef UNIQUE_FD_H
#define UNIQUE_FD_H

#include <stdio.h>

#include <memory>

class unique_fd {
  public:
    unique_fd(int fd) : fd_(fd) { }

    unique_fd(unique_fd&& uf) {
        fd_ = uf.fd_;
        uf.fd_ = -1;
    }

    ~unique_fd() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    int get() {
        return fd_;
    }

    // Movable.
    unique_fd& operator=(unique_fd&& uf) {
        fd_ = uf.fd_;
        uf.fd_ = -1;
        return *this;
    }

    explicit operator bool() const {
        return fd_ != -1;
    }

  private:
    int fd_;

    // Non-copyable.
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
};

#endif  // UNIQUE_FD_H
