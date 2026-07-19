#include "control.hpp"

#include "automap.hpp"
#include "graphics/graphics.hpp"
#include "vm/events.hpp"
#include "vm/objects.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace THIRDEYE::control {

namespace {

int gListenFd = -1;
int gClientFd = -1;
std::string gRxBuf;
std::string gCtlPath;
// Deferred left/right click release: countdown of pump ticks; -1 = idle.
// Mirrors the AUTOWALK phase-5 release (see engine.cpp) -- the SOP polls
// button state across pumps and misses an instant release.
int gReleaseIn = -1;

void closeClient() {
    if (gClientFd >= 0) {
        ::close(gClientFd);
        gClientFd = -1;
    }
    gRxBuf.clear();
}

void writeAll(int fd, std::string_view s) {
    while (!s.empty()) {
#ifdef MSG_NOSIGNAL
        ssize_t n = ::send(fd, s.data(), s.size(), MSG_NOSIGNAL);
#else
        ssize_t n = ::send(fd, s.data(), s.size(), 0); // SIGPIPE ignored in init()
#endif
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            closeClient();
            return;
        }
        s.remove_prefix(static_cast<size_t>(n));
    }
}

// Parse a hex string, tolerating "0x" prefix. Returns false on empty/garbage.
bool parseHex(const std::string &s, long &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long v = std::strtol(s.c_str(), &end, 16);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parseInt(const std::string &s, int &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

std::string handle(const std::vector<std::string> &tok,
                   GRAPHICS::Graphics *gfx, VM::EventSystem &events,
                   VM::ObjectSystem & /*objects*/) {
    if (tok.empty()) return "ok\n";
    const std::string &verb = tok[0];

    if (verb == "ping") return "ok\n";

    if (verb == "key") {
        long code = 0;
        if (tok.size() != 2 || !parseHex(tok[1], code))
            return "err key <hex>\n";
        events.postEvent(0, VM::SYS_KEYDOWN, static_cast<int32_t>(code));
        return "ok\n";
    }

    if (verb == "click") {
        if (tok.size() != 4 || (tok[1] != "L" && tok[1] != "R"))
            return "err click <L|R> <x> <y>\n";
        int x = 0, y = 0;
        if (!parseInt(tok[2], x) || !parseInt(tok[3], y))
            return "err click x/y not int\n";
        if (!gfx) return "err headless (no gfx)\n";
        // Coords are logical 320x200; mouseToLogical clamps to window space
        // (AUTOWALK feeds it window coords too, but logical->logical is a
        // no-op after the identity path). Match AUTOWALK exactly.
        int lx = 0, ly = 0;
        gfx->mouseToLogical(x, y, lx, ly);
        events.mouseMove(lx, ly);
        events.mouseButton(tok[1] == "L", tok[1] == "R");
        gReleaseIn = 5; // ~5 pumps, same as AUTOWALK phase-5
        return "ok\n";
    }

    if (verb == "map") {
        THIRDEYE::automap::toggle();
        return "ok\n";
    }

    if (verb == "dump") {
        if (tok.size() != 2) return "err dump <path>\n";
        if (!gfx) return "err headless (no gfx)\n";
        gfx->saveScreenshot(tok[1]);
        return "ok\n";
    }

    return "err unknown verb '" + verb + "'\n";
}

} // namespace

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' ||
                                    line[i] == '\r' || line[i] == '\n'))
            ++i;
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' &&
               line[i] != '\r' && line[i] != '\n')
            ++i;
        if (start < i) out.emplace_back(line.substr(start, i - start));
    }
    return out;
}

void init(const std::string &path) {
    if (path.empty()) return;
    if (gListenFd >= 0) shutdown();

    // Ignore SIGPIPE globally so a client disconnect mid-write doesn't kill
    // the process on platforms without MSG_NOSIGNAL (macOS pre-10.2, etc).
    std::signal(SIGPIPE, SIG_IGN);

    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        std::cerr << "[ctl] path too long: " << path << "\n";
        return;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[ctl] socket: " << std::strerror(errno) << "\n";
        return;
    }
    ::unlink(path.c_str()); // stale file from prior run
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[ctl] bind " << path << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return;
    }
    if (::listen(fd, 1) < 0) {
        std::cerr << "[ctl] listen: " << std::strerror(errno) << "\n";
        ::close(fd);
        ::unlink(path.c_str());
        return;
    }
    // Non-blocking accept() -- pump() spins the accept per tick.
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    gListenFd = fd;
    gCtlPath = path;
    std::cerr << "[ctl] listening on " << path << "\n";
}

void pump(GRAPHICS::Graphics *gfx, VM::EventSystem &events,
          VM::ObjectSystem &objects) {
    if (gListenFd < 0) return;

    // Deferred click release. Runs before we handle new commands so a
    // client can `click` then `dump` a few pumps later and see the effect.
    if (gReleaseIn > 0) {
        if (--gReleaseIn == 0) {
            events.mouseButton(false, false);
            gReleaseIn = -1;
        }
    }

    // Drain existing client FIRST -- a client that connected, sent a command,
    // and closed will only show its EOF on the next recv(). If we accept()
    // before that, a fast connect/close/connect sequence looks like an
    // overlap and the new client wrongly gets `err busy`.
    if (gClientFd >= 0) {
        char buf[512];
        while (true) {
            ssize_t n = ::recv(gClientFd, buf, sizeof(buf), 0);
            if (n > 0) {
                gRxBuf.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) { closeClient(); break; } // client closed
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            closeClient();
            break;
        }
        size_t nl;
        while (gClientFd >= 0 && (nl = gRxBuf.find('\n')) != std::string::npos) {
            std::string line = gRxBuf.substr(0, nl);
            gRxBuf.erase(0, nl + 1);
            std::string reply = handle(tokenize(line), gfx, events, objects);
            writeAll(gClientFd, reply);
        }
        // Bound the receive buffer -- reject one absurdly long line.
        if (gRxBuf.size() > 64 * 1024) {
            writeAll(gClientFd, "err line too long\n");
            closeClient();
        }
    }

    // Accept new clients now that any dead existing one has been closed.
    // Second live client gets `err busy` (docs/control_channel.md Q#1).
    while (true) {
        int c = ::accept(gListenFd, nullptr, nullptr);
        if (c < 0) break;
        int flags = ::fcntl(c, F_GETFL, 0);
        ::fcntl(c, F_SETFL, flags | O_NONBLOCK);
        if (gClientFd >= 0) {
            const char *busy = "err busy\n";
            (void)::send(c, busy, std::strlen(busy), 0);
            ::close(c);
        } else {
            gClientFd = c;
        }
    }
}

void shutdown() {
    closeClient();
    if (gListenFd >= 0) {
        ::close(gListenFd);
        gListenFd = -1;
    }
    if (!gCtlPath.empty()) {
        ::unlink(gCtlPath.c_str());
        gCtlPath.clear();
    }
    gReleaseIn = -1;
}

} // namespace THIRDEYE::control
