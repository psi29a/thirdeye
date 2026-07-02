#include "internal.hpp"

#include "../vm/objects.hpp"

#include <iostream>
#include <streambuf>
#include <string>

namespace THIRDEYE::runtime {

bool gRtTrace = false;
bool gCompassDirty = false;
bool gPerf = false;
int gDrawCount = 0;
long gDrawNanos = 0;
std::chrono::steady_clock::time_point gLastPresent;
std::chrono::steady_clock::time_point gBootStart;
bool gFirstPresentLogged = false;

std::ostream &rt() {
	struct NullBuf : std::streambuf { int overflow(int c) override { return c; } };
	static NullBuf nb;
	static std::ostream null(&nb);
	return gRtTrace ? std::cout : null;
}

uint8_t *staticBytePtr(Context &ctx, VM::Value addr, uint32_t size) {
	VM::Addr a = VM::decodeAddr(addr);
	if (a.space != VM::AddrSpace::Static && a.space != VM::AddrSpace::Extern)
		return nullptr;
	try {
		return ctx.objects.staticsPtr(a.obj, a.offset, size);
	} catch (const VM::VmError &) {
		return nullptr; // dead slot / OOB: caller treats as unresolvable
	}
}

std::string formatSop(const std::string &fmt, const std::vector<VM::Value> &args,
                      size_t start, VM::Interpreter &vm) {
	std::string out;
	size_t ai = start;
	for (size_t i = 0; i < fmt.size(); ++i) {
		if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
		char conv = fmt[++i];
		if (conv == '%') { out += '%'; continue; }
		VM::Value a = ai < args.size() ? args[ai++] : 0;
		if (conv == 'd' || conv == 'u' || conv == 'i')
			out += std::to_string(a);
		else if (conv == 's')
			out += vm.readString(a);
		else { out += '%'; out += conv; }
	}
	return out;
}

} // namespace THIRDEYE::runtime
