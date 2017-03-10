//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// XFAIL: libcpp-no-exceptions
// <memory>

// allocator:
// pointer allocate(size_type n, allocator<void>::const_pointer hint=0);

#include <memory>
#include <cassert>

template <typename T>
void test_max(size_t count)
{
    std::allocator<T> a;
    try {
        a.allocate(count);
        assert(false);
    } catch (const std::exception &) {
    }
}

int main()
{
    {  // Bug 26812 -- allocating too large
        typedef double T;
        std::allocator<T> a;
        test_max<T> (a.max_size() + 1);                // just barely too large
        test_max<T> (a.max_size() * 2);                // significantly too large
        test_max<T> (((size_t) -1) / sizeof(T) + 1);   // multiply will overflow
        test_max<T> ((size_t) -1);                     // way too large
    }

    {
        typedef const double T;
        std::allocator<T> a;
        test_max<T> (a.max_size() + 1);                // just barely too large
        test_max<T> (a.max_size() * 2);                // significantly too large
        test_max<T> (((size_t) -1) / sizeof(T) + 1);   // multiply will overflow
        test_max<T> ((size_t) -1);                     // way too large
    }
}
