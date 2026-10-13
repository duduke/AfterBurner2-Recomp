#pragma once
#include <memory>
#include <vector>
#include <string>
namespace Frontend {
struct DecodedMusic {std::vector<float> stereo;};
std::shared_ptr<const DecodedMusic> decode_music(const std::string& path,std::string& error);
}
