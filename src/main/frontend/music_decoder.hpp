#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include <string>
namespace Frontend {
struct DecodedMusic {std::vector<float> stereo;};
std::shared_ptr<const DecodedMusic> decode_music(const std::string& path,std::string& error);
// Same formats and limits, for audio read from a stage pack or ZIP.
std::shared_ptr<const DecodedMusic> decode_music_memory(const void* data,size_t size,std::string& error);
}
