#ifndef THIRDEYE_CONTROL_HPP
#define THIRDEYE_CONTROL_HPP

// Live control channel -- see docs/control_channel.md.
// Phase 1: ping / key / click / map / dump over a Unix domain socket, polled
// from pumpHost. Zero cost when THIRDEYE_CTL is unset.

#include <string>
#include <string_view>
#include <vector>

namespace GRAPHICS { class Graphics; }
namespace VM { class EventSystem; class ObjectSystem; }

namespace THIRDEYE::control {

// Bind a Unix stream socket at `path` (unlinks any stale file). No-op on
// empty path. Idempotent; safe to call after shutdown.
void init(const std::string &path);

// Poll the listen fd + any connected client. Called once per pumpHost tick
// (~30 Hz). No-op if init wasn't called or bind failed.
void pump(GRAPHICS::Graphics *gfx, VM::EventSystem &events,
          VM::ObjectSystem &objects);

// Close socket and unlink the file. Safe to call multiple times.
void shutdown();

// Pure: split a line on whitespace. Exposed for unit tests.
std::vector<std::string> tokenize(std::string_view line);

} // namespace THIRDEYE::control

#endif // THIRDEYE_CONTROL_HPP
