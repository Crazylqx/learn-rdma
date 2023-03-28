#pragma once
#include <cassert>
#include <cstdlib>
#include <iostream>

inline void check_err(int err, const char *msg) {
    if (err != 0) {
        std::cerr << "Error (" << errno << ") on " << msg << std::endl;
        abort();
    }
}

#define CHECK(X) check_err(X, #X)
#define ASSERT(X) assert(X)
