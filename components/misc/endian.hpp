#ifndef MISC_ENDIAN_HPP
#define MISC_ENDIAN_HPP

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>

namespace Misc {

// Load a little-endian integer from an unaligned byte buffer. Compiles to a
// single mov on LE hosts (x86_64, arm64 -- everywhere we build today); the
// byteswap branch is dead code on LE and keeps the read correct on a
// hypothetical BE host. Avoids the strict-aliasing UB of
// reinterpret_cast<const T*>(byte_ptr) AND any unaligned-access trap.
template <std::integral T>
constexpr T loadLE(const uint8_t *p) {
	T v;
	std::memcpy(&v, p, sizeof(T));
	if constexpr (std::endian::native != std::endian::little)
		v = std::byteswap(v);
	return v;
}

} // namespace Misc

#endif // MISC_ENDIAN_HPP
