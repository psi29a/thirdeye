#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
#
# LZEXE v0.91 unpacker -- Python port of Mitsugu Kurizono's `unlzexe`
# (Bontchev v0.8 source path -- a 1990s DOS utility itself released without
# any copyright reservation).
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any
# warranty. See <http://creativecommons.org/publicdomain/zero/1.0/> for
# the CC0 1.0 Universal Public Domain Dedication, which applies in
# jurisdictions that do not recognize "public domain" directly.
"""LZEXE v0.91 unpacker -- Python port of Mitsugu Kurizono's `unlzexe`."""
import sys, struct

def unpack_lzexe(packed: bytes) -> bytes:
    ihead = list(struct.unpack('<14H', packed[:28]))
    assert ihead[0] in (0x5A4D, 0x4D5A)
    assert ihead[0x0D] == 0
    assert ihead[0x0C] == 0x1C
    assert packed[0x1C:0x20] == b'LZ91', 'not LZEXE v0.91'

    # Info block at e_cs:0
    info_off = (ihead[0x0B] + ihead[4]) << 4
    inf = list(struct.unpack('<8H', packed[info_off:info_off + 16]))

    ohead = ihead[:]
    ohead[0x0A] = inf[0]; ohead[0x0B] = inf[1]      # IP, CS
    ohead[0x08] = inf[2]; ohead[0x07] = inf[3]      # SP, SS
    ohead[0x0C] = 0x1C

    # Decompress reloc table (v0.91 format, starting at info_off + 0x158)
    out_relocs = bytearray()
    pos = info_off + 0x158
    rel_off = 0; rel_seg = 0; rel_count = 0
    while True:
        span = packed[pos]; pos += 1
        if span == 0:
            span = packed[pos] | (packed[pos+1] << 8); pos += 2
            if span == 0:
                rel_seg = (rel_seg + 0x0FFF) & 0xFFFF
                continue
            elif span == 1:
                break
        rel_off = (rel_off + span) & 0xFFFF
        rel_seg = (rel_seg + ((rel_off & ~0x0F) >> 4)) & 0xFFFF
        rel_off &= 0x0F
        out_relocs += struct.pack('<HH', rel_off, rel_seg)
        rel_count += 1
    ohead[3] = rel_count

    # Pad to 512-byte boundary
    out_hdr_bytes = 0x1C + len(out_relocs)
    pad = (0x200 - out_hdr_bytes) & 0x1FF
    ohead[4] = (out_hdr_bytes + pad) >> 4

    # Decompress load module
    fpos_src = (ihead[0x0B] - inf[4] + ihead[4]) << 4
    src = packed[fpos_src:]
    si = 0
    out = bytearray()

    bits = src[si] | (src[si+1] << 8); si += 2
    bit_count = 16
    def getbit():
        nonlocal bits, bit_count, si
        b = bits & 1
        bit_count -= 1
        if bit_count == 0:
            bits = src[si] | (src[si+1] << 8); si += 2
            bit_count = 16
        else:
            bits >>= 1
        return b

    while True:
        if getbit():
            out.append(src[si]); si += 1
            continue
        if not getbit():
            length = (getbit() << 1) | getbit()
            length += 2
            span = src[si]; si += 1
            span |= 0xFF00
        else:
            span = src[si]; si += 1
            length = src[si]; si += 1
            span |= ((length & ~0x07) << 5) | 0xE000
            length = (length & 0x07) + 2
            if length == 2:
                length = src[si]; si += 1
                if length == 0:
                    break
                if length == 1:
                    continue
                length += 1
        if span & 0x8000:
            span -= 0x10000
        for _ in range(length):
            out.append(out[len(out) + span])

    loadsize = len(out)

    if ihead[6] != 0:
        adjust = inf[5] + ((inf[6] + 15) >> 4) + 9
        ohead[5] = (ohead[5] - adjust) & 0xFFFF
        if ihead[6] != 0xFFFF:
            ohead[6] = (ohead[6] - (ihead[5] - ohead[5])) & 0xFFFF

    total = loadsize + (ohead[4] << 4)
    ohead[1] = total & 0x1FF
    ohead[2] = (total + 0x1FF) >> 9

    new_header = struct.pack('<14H', *ohead)
    result = bytearray(new_header)
    result += out_relocs
    result += b'\x00' * pad
    result += out
    return bytes(result)


if __name__ == '__main__':
    packed = open(sys.argv[1], 'rb').read()
    unpacked = unpack_lzexe(packed)
    open(sys.argv[2], 'wb').write(unpacked)
    print(f'{sys.argv[1]} ({len(packed)}) -> {sys.argv[2]} ({len(unpacked)})')
