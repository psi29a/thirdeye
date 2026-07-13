#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ArjEntry {
    std::string name;           // stored filename (no path in EOB3's sets)
    int volume = 0;             // 0-based index of the volume it starts in
    std::vector<uint8_t> data;
};

// Extract a (possibly multi-volume) ARJ archive. Pass the raw volume file
// contents in order (DATA1.ARJ, DATA2.ARJ, …); files split across volumes
// are stitched back together. Every file's CRC32 is verified against the
// header. Returns false and sets err on any parse/decode/CRC failure.
bool unarjExtract(const std::vector<std::vector<uint8_t>>& volumes,
                  std::vector<ArjEntry>& out, std::string& err);
