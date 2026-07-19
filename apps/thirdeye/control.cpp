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

// Class numbers / static offsets -- the docs/control_channel.md table; all
// verified against lvl_tmp.cpp / automap.cpp / runtime/eye.cpp usage.
constexpr uint16_t kKernelCls   = 1382;
constexpr uint16_t kDungeonCls  = 1381;
constexpr uint16_t kEntitiesCls = 1370; // W:place@0 B:x@2 B:y@3 B:lvl@4 B:region@5 W:next@6
constexpr uint16_t kPcCls       = 1369; // name@137(20B) PCstat@161 hpts@171 hmax@173
constexpr uint16_t kItemsBase   = 1371;
constexpr uint16_t kNpcBase     = 1622; // W:hitpts@3 (daesop -k EYE.RES 1622 EXPT)
constexpr int kChainGuard = 2000;

// Read a little-endian field from a class static block; `def` is fallback on
// dead slot / OOB (classStaticPtr throws on dead, nullptr on OOB).
int readS(VM::ObjectSystem &objects, int obj, uint16_t cls, uint32_t off,
          int n, int def = -1) {
    try {
        if (uint8_t *p = objects.classStaticPtr(obj, cls, off, n)) {
            int v = 0;
            for (int i = 0; i < n; ++i) v |= p[i] << (8 * i);
            return n == 2 ? static_cast<int16_t>(v) : v;
        }
    } catch (const std::exception &) {}
    return def;
}

std::string entityName(VM::ObjectSystem &objects, int obj) {
    std::string name;
    try {
        if (uint8_t *p = objects.classStaticPtr(obj, kPcCls, 137, 20)) {
            for (int i = 0; i < 20 && p[i]; ++i)
                name += (p[i] == ' ') ? '_' : static_cast<char>(p[i]);
        }
    } catch (const std::exception &) {}
    return name.empty() ? "?" : name;
}

std::string handle(const std::vector<std::string> &tok,
                   GRAPHICS::Graphics *gfx, VM::EventSystem &events,
                   VM::ObjectSystem &objects) {
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

    // ---- Phase 2: observe --------------------------------------------------

    if (verb == "party") {
        int kernel = objects.firstObjectOfClass(kKernelCls);
        if (kernel < 0) return "err no kernel (not in game yet)\n";
        char line[128];
        std::snprintf(line, sizeof(line), "pose x=%d y=%d fdir=%d lvl=%d\n",
                      readS(objects, kernel, kKernelCls, 243, 1),
                      readS(objects, kernel, kKernelCls, 244, 1),
                      readS(objects, kernel, kKernelCls, 245, 1),
                      readS(objects, kernel, kKernelCls, 246, 1));
        std::string out = line;
        for (uint32_t slot = 0; slot < 6; ++slot) {
            int pc = readS(objects, kernel, kKernelCls, 229 + slot * 2, 2);
            if (pc < 0 || objects.classOf(pc) == 0xFFFF) continue;
            std::snprintf(line, sizeof(line),
                          "pc slot=%u obj=%d name=%s hp=%d/%d status=0x%02x\n",
                          slot, pc, entityName(objects, pc).c_str(),
                          readS(objects, pc, kPcCls, 171, 2),
                          readS(objects, pc, kPcCls, 173, 2),
                          readS(objects, pc, kPcCls, 161, 2, 0) & 0xFFFF);
            out += line;
        }
        return out + "ok\n";
    }

    if (verb == "cell") {
        int x = 0, y = 0;
        if (tok.size() != 3 || !parseInt(tok[1], x) || !parseInt(tok[2], y))
            return "err cell <x> <y>\n";
        if (x < 0 || x > 31 || y < 0 || y > 31) return "err coords 0..31\n";
        int dn = objects.firstObjectOfClass(kDungeonCls);
        if (dn < 0) return "err no dungeon\n";
        std::string out;
        for (int plane = 0; plane < 3; ++plane) {
            int head = readS(objects, dn, kDungeonCls,
                             1024u + plane * 2048u + y * 64u + x * 2u, 2);
            char line[64];
            std::snprintf(line, sizeof(line), "plane%d head=%d", plane, head);
            out += line;
            if (head >= 0) {
                out += " chain=";
                int cur = head, hops = 0;
                while (cur >= 0) {
                    if (++hops > kChainGuard)
                        return "err chain loop at plane " +
                               std::to_string(plane) + "\n";
                    std::snprintf(line, sizeof(line), "%s%d:%d",
                                  hops > 1 ? "," : "", cur,
                                  objects.classOf(cur));
                    out += line;
                    cur = readS(objects, cur, kEntitiesCls, 6, 2);
                }
            }
            out += '\n';
        }
        return out + "ok\n";
    }

    if (verb == "items" || verb == "monsters") {
        bool wantItems = (verb == "items");
        int kernel = objects.firstObjectOfClass(kKernelCls);
        if (kernel < 0) return "err no kernel (not in game yet)\n";
        int lvl = readS(objects, kernel, kKernelCls, 246, 1);
        std::string out;
        for (int id = 0; id < VM::ObjectSystem::kNumEntities; ++id) {
            uint16_t cls = objects.classOf(id);
            if (cls == 0xFFFF) continue;
            if (!objects.isSubclassOf(cls, wantItems ? kItemsBase : kNpcBase))
                continue;
            if (readS(objects, id, kEntitiesCls, 4, 1) != lvl) continue;
            char line[96];
            if (wantItems) {
                if (readS(objects, id, kEntitiesCls, 0, 2) != -1)
                    continue; // held/equipped, not on the floor
                std::snprintf(line, sizeof(line),
                              "item obj=%d cls=%d x=%d y=%d region=%d\n",
                              id, cls,
                              readS(objects, id, kEntitiesCls, 2, 1),
                              readS(objects, id, kEntitiesCls, 3, 1),
                              readS(objects, id, kEntitiesCls, 5, 1));
            } else {
                std::snprintf(line, sizeof(line),
                              "npc obj=%d cls=%d x=%d y=%d hp=%d\n",
                              id, cls,
                              readS(objects, id, kEntitiesCls, 2, 1),
                              readS(objects, id, kEntitiesCls, 3, 1),
                              readS(objects, id, kNpcBase, 3, 2));
            }
            out += line;
        }
        return out + "ok\n";
    }

    if (verb == "obj") {
        int id = 0;
        if (tok.size() != 2 || !parseInt(tok[1], id))
            return "err obj <id>\n";
        uint16_t cls = objects.classOf(id);
        if (cls == 0xFFFF) return "err dead object\n";
        char line[128];
        std::snprintf(line, sizeof(line),
                      "cls=%d place=%d x=%d y=%d lvl=%d region=%d next=%d prev=%d\n",
                      cls,
                      readS(objects, id, kEntitiesCls, 0, 2),
                      readS(objects, id, kEntitiesCls, 2, 1),
                      readS(objects, id, kEntitiesCls, 3, 1),
                      readS(objects, id, kEntitiesCls, 4, 1),
                      readS(objects, id, kEntitiesCls, 5, 1),
                      readS(objects, id, kEntitiesCls, 6, 2),
                      readS(objects, id, kEntitiesCls, 8, 2));
        return std::string(line) + "ok\n";
    }

    if (verb == "peek") {
        int obj = 0, off = 0, n = 0;
        if (tok.size() != 4 || !parseInt(tok[1], obj) ||
            !parseInt(tok[2], off) || !parseInt(tok[3], n))
            return "err peek <obj> <off> <n>\n";
        if (n < 1 || n > 256) return "err n 1..256\n";
        if (off < 0) return "err off >= 0\n";
        uint8_t *p = nullptr;
        try {
            p = objects.staticsPtr(obj, static_cast<uint32_t>(off),
                                   static_cast<uint32_t>(n));
        } catch (const std::exception &e) {
            return std::string("err ") + e.what() + "\n";
        }
        if (!p) return "err out of range\n";
        std::string out;
        char hex[8];
        for (int i = 0; i < n; ++i) {
            std::snprintf(hex, sizeof(hex), "%02x", p[i]);
            out += hex;
            out += ((i % 16 == 15) || i == n - 1) ? '\n' : ' ';
        }
        return out + "ok\n";
    }

    if (verb == "send") {
        int obj = 0, msg = 0;
        if (tok.size() < 3 || !parseInt(tok[1], obj) || !parseInt(tok[2], msg))
            return "err send <obj> <msg> [args]\n";
        std::vector<VM::Value> args;
        for (size_t i = 3; i < tok.size(); ++i) {
            int v = 0;
            if (!parseInt(tok[i], v)) return "err arg not int\n";
            args.emplace_back(v);
        }
        if (objects.classOf(obj) == 0xFFFF) return "err dead object\n";
        // Runs SOP bytecode -- can corrupt game state. Debug tool; caveat emptor.
        try {
            VM::Value r = objects.send(obj, msg, std::move(args));
            return "result " + std::to_string(r) + "\nok\n";
        } catch (const std::exception &e) {
            return std::string("err ") + e.what() + "\n";
        }
    }

    if (verb == "save")
        return "err unimplemented (phase 3: drive the camp flow via clicks)\n";

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
