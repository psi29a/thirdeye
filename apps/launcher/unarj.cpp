// In-memory multi-volume ARJ extractor for the launcher's Internet Archive
// flow (the EOB3 floppies ship as DATA1..6.ARJ, with EYE.RES/DARK.GFF/
// LICH.GFF split across volume boundaries).
//
// The decoder is a port of decode.c from Andrew Belov's open-source ARJ
// 3.10 (GPL-2+, via github.com/joncampbell123/arj) — methods 0 (stored),
// 1-3 (LZ77 + dynamic Huffman, 26 KB dictionary) and 4 (fast, gamma-coded).
// Ported faithfully: same table sizes, same bit-reader semantics (16-bit
// sliding bitbuf preloaded with fillbuf(16)). Do not "fix" the algorithm.

#include "unarj.hpp"

#include <cstring>
#include <stdexcept>

namespace {

// --- constants (defines.h/environ.h of GPL arj 3.10) ---
constexpr int CODE_BIT = 16;
constexpr int THRESHOLD = 3;
constexpr int MAXMATCH = 256;
constexpr int DICSIZ = 26624;
constexpr int FDICSIZ = 32768;          // method-4 dictionary
constexpr int NC = 255 + MAXMATCH + 2 - THRESHOLD;  // 510
constexpr int MAXDICBIT = 16;
constexpr int NP = MAXDICBIT + 1;       // 17
constexpr int NT = CODE_BIT + 3;        // 19
constexpr int NPT = NT;                 // max(NT, NP)
constexpr int CBIT = 9;
constexpr int PBIT = 5;
constexpr int TBIT = 5;
constexpr int CTABLESIZE = 4096;
constexpr int PTABLESIZE = 256;

// --- header flags / file types ---
constexpr uint8_t FLAG_GARBLED = 0x01;
constexpr uint8_t FLAG_VOLUME  = 0x04;  // file continues in the next volume
constexpr uint8_t FLAG_EXTFILE = 0x08;  // continuation of the previous volume
constexpr uint8_t TYPE_MAIN_HEADER = 2;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
        | uint32_t(p[3]) << 24;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
            table[i] = c;
        }
    }
    crc = ~crc;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

struct Piece {
    std::string name;
    int volume = 0;
    uint8_t flags = 0, method = 0, fileType = 0;
    uint32_t compSize = 0, origSize = 0, crc = 0;
    const uint8_t* data = nullptr;
};

[[noreturn]] void corrupt(const char* what) {
    throw std::runtime_error(std::string("corrupt ARJ data: ") + what);
}

// The decoder proper. One instance decodes one volume piece: ARJ restarts
// the compressor at every volume boundary (which is why each piece header
// carries its own orig_size and CRC), so split files are decoded piecewise
// and the outputs concatenated.
class Decoder {
public:
    Decoder(const Piece& piece) {
        m_segs.emplace_back(piece.data, piece.compSize);
        m_compRemain = piece.compSize;
        // decode_start_stub()
        fillbuf(16);
    }

    void decodeLzh(uint64_t origsize, std::vector<uint8_t>& out);  // methods 1-3
    void decodeFast(uint64_t origsize, std::vector<uint8_t>& out); // method 4

private:
    std::vector<std::pair<const uint8_t*, size_t>> m_segs;
    size_t m_seg = 0, m_segPos = 0;
    uint64_t m_compRemain = 0;

    uint16_t bitbuf = 0;
    uint8_t byte_buf = 0;
    int bitcount = 0;

    uint8_t c_len[NC] = {};
    uint8_t pt_len[NPT] = {};
    uint16_t c_table[CTABLESIZE] = {};
    uint16_t pt_table[PTABLESIZE] = {};
    uint16_t left[2 * NC - 1] = {};
    uint16_t right[2 * NC - 1] = {};
    short blocksize = 0;

    uint8_t nextRawByte() {
        while (m_seg < m_segs.size() && m_segPos >= m_segs[m_seg].second) {
            ++m_seg;
            m_segPos = 0;
        }
        if (m_seg >= m_segs.size())
            corrupt("compressed stream truncated");
        return m_segs[m_seg].first[m_segPos++];
    }

    void fillbuf(int n) {
        while (bitcount < n) {
            bitbuf = uint16_t((bitbuf << bitcount)
                              | (unsigned(byte_buf) >> (8 - bitcount)));
            n -= bitcount;
            if (m_compRemain > 0) {
                m_compRemain--;
                byte_buf = nextRawByte();
            } else
                byte_buf = 0;
            bitcount = 8;
        }
        bitcount -= n;
        bitbuf = uint16_t((bitbuf << n) | (byte_buf >> (8 - n)));
        byte_buf = uint8_t(byte_buf << n);
    }

    int getbits(int n) {
        int rc = bitbuf >> (CODE_BIT - n);
        fillbuf(n);
        return rc;
    }

    void make_table(int nchar, const uint8_t* bitlen, int tablebits,
                    uint16_t* table, int tablesize);
    void read_pt_len(int nn, int nbit, int i_special);
    void read_c_len();
    uint16_t decode_c();
    uint16_t decode_p();
    short decode_ptr();
    short decode_len();
};

void Decoder::make_table(int nchar, const uint8_t* bitlen, int tablebits,
                         uint16_t* table, int tablesize) {
    uint16_t count[17], weight[17], start[18];
    unsigned i;

    for (i = 1; i <= 16; i++)
        count[i] = 0;
    for (i = 0; int(i) < nchar; i++)
        count[bitlen[i]]++;
    start[1] = 0;
    for (i = 1; i <= 16; i++)
        start[i + 1] = uint16_t(start[i] + (count[i] << (16 - i)));
    if (start[17] != 0)                 // must wrap to exactly 2^16
        corrupt("bad Huffman table");
    unsigned jutbits = 16 - unsigned(tablebits);
    for (i = 1; int(i) <= tablebits; i++) {
        start[i] >>= jutbits;
        weight[i] = uint16_t(1 << (tablebits - int(i)));
    }
    while (i <= 16) {
        weight[i] = uint16_t(1 << (16 - i));
        i++;
    }
    i = start[tablebits + 1] >> jutbits;
    if (i != 0) {
        unsigned k = 1u << tablebits;
        while (i != k)
            table[i++] = 0;
    }
    unsigned avail = unsigned(nchar);
    unsigned mask = 1u << (15 - tablebits);
    for (unsigned ch = 0; int(ch) < nchar; ch++) {
        unsigned len = bitlen[ch];
        if (len == 0)
            continue;
        unsigned k = start[len];
        unsigned nextcode = k + weight[len];
        if (int(len) <= tablebits) {
            if (nextcode > unsigned(tablesize))
                corrupt("bad Huffman table");
            for (i = k; i < nextcode; i++)
                table[i] = uint16_t(ch);
        } else {
            uint16_t* p = &table[k >> jutbits];
            i = len - unsigned(tablebits);
            while (i != 0) {
                if (*p == 0) {
                    if (avail >= 2 * NC - 1)   // hardened vs. original
                        corrupt("bad Huffman table");
                    right[avail] = left[avail] = 0;
                    *p = uint16_t(avail);
                    avail++;
                }
                if (k & mask)
                    p = &right[*p];
                else
                    p = &left[*p];
                k <<= 1;
                i--;
            }
            *p = uint16_t(ch);
        }
        start[len] = uint16_t(nextcode);
    }
}

void Decoder::read_pt_len(int nn, int nbit, int i_special) {
    int i, n;
    short c;

    n = getbits(nbit);
    if (n == 0) {
        c = short(getbits(nbit));
        for (i = 0; i < nn; i++)
            pt_len[i] = 0;
        for (i = 0; i < PTABLESIZE; i++)
            pt_table[i] = uint16_t(c);
    } else {
        i = 0;
        if (n >= NPT)
            n = NPT;
        while (i < n) {
            c = short(bitbuf >> 13);
            if (c == 7) {
                unsigned mask = 1u << 12;
                while (mask & bitbuf) {
                    mask >>= 1;
                    c++;
                }
            }
            fillbuf((c < 7) ? 3 : int(c - 3));
            pt_len[i++] = uint8_t(c);
            if (i == i_special) {
                c = short(getbits(2));
                while (--c >= 0)
                    pt_len[i++] = 0;
            }
        }
        while (i < nn)
            pt_len[i++] = 0;
        make_table(nn, pt_len, 8, pt_table, PTABLESIZE);
    }
}

void Decoder::read_c_len() {
    short i, c, n;

    n = short(getbits(CBIT));
    if (n == 0) {
        c = short(getbits(CBIT));
        for (i = 0; i < NC; i++)
            c_len[i] = 0;
        for (i = 0; i < CTABLESIZE; i++)
            c_table[i] = uint16_t(c);
    } else {
        i = 0;
        while (i < n) {
            c = short(pt_table[bitbuf >> 8]);
            if (c >= NT) {
                unsigned mask = 1u << 7;
                do {
                    if (bitbuf & mask)
                        c = short(right[c]);
                    else
                        c = short(left[c]);
                    mask >>= 1;
                    if (!mask && c >= NT)      // hardened vs. original
                        corrupt("bad code-length tree");
                } while (c >= NT);
            }
            fillbuf(int(pt_len[c]));
            if (c <= 2) {
                if (c == 0)
                    c = 1;
                else if (c == 1)
                    c = short(getbits(4) + 3);
                else
                    c = short(getbits(CBIT) + 20);
                while (--c >= 0)
                    c_len[i++] = 0;
            } else
                c_len[i++] = uint8_t(c - 2);
        }
        while (i < NC)
            c_len[i++] = 0;
        make_table(NC, c_len, 12, c_table, CTABLESIZE);
    }
}

uint16_t Decoder::decode_c() {
    if (blocksize == 0) {
        blocksize = short(getbits(CODE_BIT));
        read_pt_len(NT, TBIT, 3);
        read_c_len();
        read_pt_len(NP, PBIT, -1);
    }
    blocksize--;
    uint16_t j = c_table[bitbuf >> 4];
    if (j >= NC) {
        unsigned mask = 1u << 3;
        do {
            if (bitbuf & mask)
                j = right[j];
            else
                j = left[j];
            mask >>= 1;
            if (!mask && j >= NC)              // hardened vs. original
                corrupt("bad character tree");
        } while (j >= NC);
    }
    fillbuf(c_len[j]);
    return j;
}

uint16_t Decoder::decode_p() {
    uint16_t j = pt_table[bitbuf >> 8];
    if (j >= NP) {
        unsigned mask = 1u << 7;
        do {
            if (bitbuf & mask)
                j = right[j];
            else
                j = left[j];
            mask >>= 1;
            if (!mask && j >= NP)              // hardened vs. original
                corrupt("bad pointer tree");
        } while (j >= NP);
    }
    fillbuf(pt_len[j]);
    if (j != 0) {
        j--;
        j = uint16_t((1 << j) + getbits(j));
    }
    return j;
}

void Decoder::decodeLzh(uint64_t origsize, std::vector<uint8_t>& out) {
    const size_t base = out.size();
    std::vector<uint8_t> text(DICSIZ);
    blocksize = 0;
    int64_t count = int64_t(origsize);
    int r = 0;
    while (count > 0) {
        uint16_t c = decode_c();
        if (c <= 255) {
            text[r] = uint8_t(c);
            count--;
            if (++r >= DICSIZ) {
                r = 0;
                out.insert(out.end(), text.begin(), text.end());
            }
        } else {
            int j = c - (255 + 1 - THRESHOLD);
            count -= j;
            int i = r - int(decode_p()) - 1;
            if (i < 0)
                i += DICSIZ;
            if (r > i && r < DICSIZ - MAXMATCH - 1) {
                while (--j >= 0)
                    text[r++] = text[i++];
            } else {
                while (--j >= 0) {
                    text[r] = text[i];
                    if (++r >= DICSIZ) {
                        r = 0;
                        out.insert(out.end(), text.begin(), text.end());
                    }
                    if (++i >= DICSIZ)
                        i = 0;
                }
            }
        }
    }
    if (r > 0)
        out.insert(out.end(), text.begin(), text.begin() + r);
    if (out.size() > base + origsize)   // a final match may overshoot the EOF
        out.resize(base + origsize);
}

short Decoder::decode_ptr() {
    short c = 0, width, plus = 0, pwr = 1 << 9;
    for (width = 9; width < 13; width++) {
        c = short(getbits(1));
        if (c == 0)
            break;
        plus = short(plus + pwr);
        pwr <<= 1;
    }
    if (width != 0)
        c = short(getbits(width));
    c = short(c + plus);
    return c;
}

short Decoder::decode_len() {
    short c = 0, width, plus = 0, pwr = 1;
    for (width = 0; width < 7; width++) {
        c = short(getbits(1));
        if (c == 0)
            break;
        plus = short(plus + pwr);
        pwr <<= 1;
    }
    if (width != 0)
        c = short(getbits(width));
    c = short(c + plus);
    return c;
}

void Decoder::decodeFast(uint64_t origsize, std::vector<uint8_t>& out) {
    const size_t base = out.size();
    std::vector<uint8_t> text(FDICSIZ);
    uint64_t ncount = 0;
    int r = 0;
    while (ncount < origsize) {
        int c = decode_len();
        if (c == 0) {
            ncount++;
            text[r] = uint8_t(bitbuf >> 8);
            fillbuf(8);
            if (++r >= FDICSIZ) {
                r = 0;
                out.insert(out.end(), text.begin(), text.end());
            }
        } else {
            int j = c - 1 + THRESHOLD;
            ncount += uint64_t(j);
            int i = r - int(decode_ptr()) - 1;
            if (i < 0)
                i += FDICSIZ;
            while (j-- > 0) {
                text[r] = text[i];
                if (++r >= FDICSIZ) {
                    r = 0;
                    out.insert(out.end(), text.begin(), text.end());
                }
                if (++i >= FDICSIZ)
                    i = 0;
            }
        }
    }
    if (r > 0)
        out.insert(out.end(), text.begin(), text.begin() + r);
    if (out.size() > base + origsize)
        out.resize(base + origsize);
}

// Walks one volume's headers, appending file pieces (main archive header and
// end-of-archive marker are consumed here). All reads bounds-checked — this
// parses downloaded data.
bool parseVolume(const std::vector<uint8_t>& vol, int volIdx,
                 std::vector<Piece>& pieces, std::string& err) {
    const size_t n = vol.size();
    size_t pos = 0;
    while (true) {
        if (pos + 4 > n) {
            err = "truncated ARJ volume";
            return false;
        }
        if (rd16(&vol[pos]) != 0xEA60) {
            err = "not an ARJ archive (bad magic)";
            return false;
        }
        const uint16_t hdrSize = rd16(&vol[pos + 2]);
        if (hdrSize == 0)               // end-of-archive marker
            return true;
        // 30 = first_hdr_size of every ARJ version; the fixed fields we read
        // below (h[0]..h[23]) must all lie inside the declared header.
        if (hdrSize < 30 || pos + 4 + hdrSize + 4 > n) {
            err = "truncated ARJ header";
            return false;
        }
        const uint8_t* h = &vol[pos + 4];
        const uint8_t firstHdrSize = h[0];

        Piece p;
        p.volume = volIdx;
        p.flags = h[4];
        p.method = h[5];
        p.fileType = h[6];
        p.compSize = rd32(h + 12);
        p.origSize = rd32(h + 16);
        p.crc = rd32(h + 20);
        if (firstHdrSize < hdrSize) {
            const uint8_t* q = h + firstHdrSize;
            const uint8_t* end = h + hdrSize;
            while (q < end && *q)
                p.name += char(*q++);
        }

        pos += 4 + hdrSize + 4;         // basic header + its CRC32
        while (true) {                  // extended headers (unused since 2.30)
            if (pos + 2 > n) {
                err = "truncated ARJ extended header";
                return false;
            }
            const uint16_t extSize = rd16(&vol[pos]);
            pos += 2;
            if (extSize == 0)
                break;
            pos += extSize + 4;
        }

        if (p.fileType == TYPE_MAIN_HEADER)
            continue;                   // archive header carries no data
        if (pos + p.compSize > n) {
            err = "truncated ARJ file data";
            return false;
        }
        p.data = &vol[pos];
        pos += p.compSize;
        pieces.push_back(std::move(p));
    }
}

} // namespace

bool unarjExtract(const std::vector<std::vector<uint8_t>>& volumes,
                  std::vector<ArjEntry>& out, std::string& err) {
    std::vector<Piece> pieces;
    for (size_t v = 0; v < volumes.size(); ++v)
        if (!parseVolume(volumes[v], int(v), pieces, err))
            return false;

    size_t i = 0;
    while (i < pieces.size()) {
        const Piece& first = pieces[i];
        if (first.flags & FLAG_EXTFILE) {
            err = "continuation piece without a start: " + first.name;
            return false;
        }
        if (first.flags & FLAG_GARBLED) {
            err = "password-protected ARJ archives are not supported";
            return false;
        }
        std::vector<const Piece*> parts{&first};
        while (parts.back()->flags & FLAG_VOLUME) {
            ++i;
            if (i >= pieces.size() || !(pieces[i].flags & FLAG_EXTFILE)
                || pieces[i].name != first.name) {
                err = "missing continuation volume for " + first.name;
                return false;
            }
            parts.push_back(&pieces[i]);
        }
        ++i;
        if (first.fileType > 1)         // directories, labels, chapters
            continue;

        uint64_t totalOrig = 0;
        for (const Piece* p : parts)
            totalOrig += p->origSize;

        ArjEntry entry;
        entry.name = first.name;
        entry.volume = first.volume;
        entry.data.reserve(totalOrig);
        for (const Piece* p : parts) {
            const size_t pieceStart = entry.data.size();
            try {
                switch (p->method) {
                case 0:                 // stored
                    entry.data.insert(entry.data.end(), p->data,
                                      p->data + p->compSize);
                    break;
                case 1:
                case 2:
                case 3: {
                    Decoder dec(*p);
                    dec.decodeLzh(p->origSize, entry.data);
                    break;
                }
                case 4: {
                    Decoder dec(*p);
                    dec.decodeFast(p->origSize, entry.data);
                    break;
                }
                default:
                    err = "unsupported ARJ method "
                        + std::to_string(p->method) + " for " + first.name;
                    return false;
                }
            } catch (const std::exception& e) {
                err = first.name + ": " + e.what();
                return false;
            }
            if (entry.data.size() - pieceStart != p->origSize
                || crc32(entry.data.data() + pieceStart, p->origSize)
                       != p->crc) {
                err = first.name + ": CRC mismatch (corrupt download?)";
                return false;
            }
        }
        out.push_back(std::move(entry));
    }
    return true;
}
