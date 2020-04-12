/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ANDROID_VOLD_KEYBUFFER_H
#define ANDROID_VOLD_KEYBUFFER_H

#include <cstring>
#include <memory>
#include <vector>

namespace android {
namespace vold {

/**
 * Variant of memset() that should never be optimized away. Borrowed from keymaster code.
 */
#ifdef __clang__
#define OPTNONE __attribute__((optnone))
#else  // not __clang__
#define OPTNONE __attribute__((optimize("O0")))
#endif  // not __clang__
inline OPTNONE void* memset_s(void* s, int c, size_t n) {
    if (!s)
        return s;
    return memset(s, c, n);
}
#undef OPTNONE

// Allocator that delegates useful work to standard one but zeroes data before deallocating.
class ZeroingAllocator : public std::allocator<char> {
    public:
    void deallocate(pointer p, size_type n)
    {
        memset_s(p, 0, n);
        std::allocator<char>::deallocate(p, n);
    }
};

// Char vector that zeroes memory when deallocating.
using KeyBuffer = std::vector<char, ZeroingAllocator>;

// Convenience methods to concatenate key buffers.
KeyBuffer operator+(KeyBuffer&& lhs, const KeyBuffer& rhs);
KeyBuffer operator+(KeyBuffer&& lhs, const char* rhs);

}  // namespace vold
}  // namespace android

#endif

