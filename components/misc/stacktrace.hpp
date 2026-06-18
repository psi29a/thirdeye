#ifndef MISC_STACKTRACE_HPP
#define MISC_STACKTRACE_HPP

// Tiny std::stacktrace fallback. C++23's <stacktrace> isn't in libc++ yet
// (Apple Clang 21 / libc++) -- when it lands, switch Misc::stacktrace() to
// just return std::to_string(std::stacktrace::current()).
//
// macOS / Linux: glibc + libSystem both ship backtrace(3) in <execinfo.h>,
// which is good enough for diagnostics from a top-level catch.
// Windows: no fallback yet -- returns an empty string.

#include <string>
#include <version>

#if __has_include(<execinfo.h>)
#  include <execinfo.h>
#  include <cstdlib>
#  include <cstring>
#endif

namespace Misc {

// Returns a human-readable backtrace from the calling frame outwards, one
// frame per line. `skip` drops the topmost N frames (default 1, to hide this
// call itself). Empty string if the platform has no implementation.
inline std::string stacktrace(int skip = 1, int maxFrames = 64) {
#if __has_include(<execinfo.h>)
	void *frames[128];
	if (maxFrames > 128) maxFrames = 128;
	int n = ::backtrace(frames, maxFrames);
	if (n <= skip)
		return {};
	char **syms = ::backtrace_symbols(frames + skip, n - skip);
	if (syms == nullptr)
		return {};
	std::string out;
	for (int i = 0; i < n - skip; ++i) {
		out += syms[i];
		out += '\n';
	}
	std::free(syms);
	return out;
#else
	(void)skip; (void)maxFrames;
	return {};
#endif
}

} // namespace Misc

#endif // MISC_STACKTRACE_HPP
