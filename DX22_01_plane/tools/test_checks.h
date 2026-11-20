#pragma once
#include <cstdlib>
#include <iostream>

// Always enabled, independent of NDEBUG, and readable in redirected test logs.
inline void TestCheck(bool passed, const char* expression, const char* file, int line)
{
    if (passed) return;
    std::cerr << file << ':' << line << ": failed: " << expression << std::endl;
    std::exit(1);
}

#undef assert
#define assert(expression) TestCheck(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
