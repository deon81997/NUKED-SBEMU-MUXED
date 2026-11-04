// Force-included when compiling the unmodified Nuked-SC55 core with DJGPP's
// GCC 12 (which predates C++23 <utility> additions). GPL-2.0-or-later.
#pragma once
#ifdef __cplusplus
#include <utility>
#if !defined(__cpp_lib_unreachable)
namespace std
{
[[noreturn]] inline void unreachable() { __builtin_unreachable(); }
}
#endif
#endif
