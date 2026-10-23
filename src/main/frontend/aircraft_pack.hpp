#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Frontend {

struct AircraftPoint {float x{},y{};};
struct AircraftRect {float x{},y{},w{},h{};};
struct AircraftRegistration {
    AircraftPoint art_pivot,native_pivot,pixels_per_native;
};
struct AircraftNozzle {
    std::string name;
    AircraftPoint art,native;
};
struct AircraftLodRegistration {
    unsigned frame_address{};
    AircraftPoint native_pivot,pixels_per_native;
};
struct AircraftPose {
    unsigned root_frame_address{},base_width{},base_height{};
    std::filesystem::path image_path;
    AircraftRect source;
    AircraftRegistration registration;
    std::vector<AircraftNozzle> nozzles;
    std::vector<AircraftLodRegistration> lods;
};

// Artist metadata only: identity and LOD membership still come from the runtime
// ROM catalog. No gameplay state or ROM image data is stored in an asset pack.
class AircraftPack {
    static constexpr size_t max_bytes=65536,max_line=4096,max_poses=64,max_lods=64,max_nozzles=8;

    static unsigned integer(std::istringstream& row) {
        std::string token;if(!(row>>token) || token.empty())throw std::invalid_argument("Missing aircraft integer");
        unsigned value{};const char* first=token.data();const char* last=first+token.size();int base=10;
        if(token.size()>2 && token[0]=='0' && (token[1]=='x' || token[1]=='X')){first+=2;base=16;}
        const auto result=std::from_chars(first,last,value,base);
        if(result.ec!=std::errc{} || result.ptr!=last)throw std::invalid_argument("Invalid aircraft integer");
        return value;
    }
    static unsigned address(std::istringstream& row) {
        const auto value=integer(row);
        if(!value || value>0xffffff || value%2)throw std::invalid_argument("Invalid aircraft frame address");
        return value;
    }
    static float scalar(std::istringstream& row) {
        float value{};
        if(!(row>>value) || !std::isfinite(value))throw std::invalid_argument("Invalid aircraft coordinate");
        return value;
    }
    static AircraftPoint point(std::istringstream& row) {return {scalar(row),scalar(row)};}
    static std::string quoted(std::istringstream& row) {
        row>>std::ws;std::string value;
        if(row.peek()!='"' || !(row>>std::quoted(value)) || value.empty())throw std::invalid_argument("Expected quoted aircraft string");
        return value;
    }
    static bool inside(AircraftPoint point,AircraftRect rect) {
        return point.x>=rect.x && point.y>=rect.y && point.x<=rect.x+rect.w && point.y<=rect.y+rect.h;
    }
    static void scale(AircraftPoint value) {
        if(value.x<1.0f/4096 || value.y<1.0f/4096 || value.x>4096 || value.y>4096)
            throw std::invalid_argument("Aircraft scale must be between 1/4096 and 4096 pixels per native unit");
    }
    static void native(AircraftPoint value,unsigned width,unsigned height) {
        if(!inside(value,{0,0,float(width),float(height)}))throw std::invalid_argument("Aircraft native point is outside reference frame");
    }
public:
    // Resolve every authored image inside the manifest directory, including
    // symlinks. Relative paths are intentional so packs remain relocatable.
    static std::filesystem::path image_file(const std::filesystem::path& manifest,const std::filesystem::path& relative) {
        if(relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.string().size()>1024)
            throw std::invalid_argument("Aircraft image path must be relative to its pack");
        for(const auto& part:relative)if(part=="..")throw std::invalid_argument("Aircraft image path escapes its pack");
        const auto directory=std::filesystem::weakly_canonical(std::filesystem::absolute(manifest).parent_path());
        const auto candidate=std::filesystem::weakly_canonical(directory/relative);
        auto root=directory.begin(),path=candidate.begin();
        for(;root!=directory.end();++root,++path)
            if(path==candidate.end() || *root!=*path)throw std::invalid_argument("Aircraft image path escapes its pack");
        if(path==candidate.end())throw std::invalid_argument("Aircraft image path is a directory");
        return candidate;
    }

    static std::vector<AircraftPose> read(const std::filesystem::path& manifest) {
        std::error_code ec;const auto size=std::filesystem::file_size(manifest,ec);
        if(ec || !size || size>max_bytes)throw std::invalid_argument("Aircraft pack must be a readable file of at most 64 KiB");
        std::ifstream file(manifest);
        if(!file)throw std::invalid_argument("Cannot open aircraft pack");
        std::vector<AircraftPose> poses;std::string line;bool header=false;size_t line_number=0;
        while(std::getline(file,line)) {
            ++line_number;
            try {
                if(line.size()>max_line)throw std::invalid_argument("Aircraft pack line is too long");
                std::istringstream row(line);row.imbue(std::locale::classic());row>>std::ws;
                if(row.eof() || row.peek()=='#')continue;
                std::string command;row>>command;
                if(!header) {
                    if(command!="aircraft-pack" || integer(row)!=1)throw std::invalid_argument("Expected aircraft-pack 1 header");
                    header=true;
                } else if(command=="pose") {
                    if(poses.size()>=max_poses)throw std::invalid_argument("Aircraft pack has too many poses");
                    AircraftPose pose;pose.root_frame_address=address(row);pose.image_path=quoted(row);
                    pose.source={scalar(row),scalar(row),scalar(row),scalar(row)};
                    pose.base_width=integer(row);pose.base_height=integer(row);
                    pose.registration={point(row),point(row),point(row)};
                    if(std::any_of(poses.begin(),poses.end(),[&](const auto& other){return other.root_frame_address==pose.root_frame_address;}))
                        throw std::invalid_argument("Duplicate aircraft pose root");
                    if(pose.source.x<0 || pose.source.y<0 || pose.source.w<=0 || pose.source.h<=0 ||
                       pose.source.x+pose.source.w>4096 || pose.source.y+pose.source.h>4096)
                        throw std::invalid_argument("Invalid aircraft source rectangle");
                    if(!pose.base_width || !pose.base_height || pose.base_width>4096 || pose.base_height>4096)
                        throw std::invalid_argument("Invalid aircraft reference dimensions");
                    if(!inside(pose.registration.art_pivot,pose.source))throw std::invalid_argument("Aircraft art pivot is outside source rectangle");
                    native(pose.registration.native_pivot,pose.base_width,pose.base_height);scale(pose.registration.pixels_per_native);
                    image_file(manifest,pose.image_path);poses.push_back(std::move(pose));
                } else if(command=="nozzle" || command=="lod") {
                    const auto root=address(row);
                    const auto found=std::find_if(poses.begin(),poses.end(),[&](const auto& pose){return pose.root_frame_address==root;});
                    if(found==poses.end())throw std::invalid_argument("Aircraft annotation has no preceding pose");
                    if(command=="nozzle") {
                        AircraftNozzle nozzle{quoted(row),point(row),point(row)};
                        if(found->nozzles.size()>=max_nozzles || nozzle.name.size()>64 ||
                           std::any_of(found->nozzles.begin(),found->nozzles.end(),[&](const auto& other){return other.name==nozzle.name;}))
                            throw std::invalid_argument("Invalid or duplicate aircraft nozzle");
                        if(!inside(nozzle.art,found->source))throw std::invalid_argument("Aircraft nozzle is outside source rectangle");
                        native(nozzle.native,found->base_width,found->base_height);found->nozzles.push_back(std::move(nozzle));
                    } else {
                        AircraftLodRegistration lod{address(row),point(row),point(row)};
                        if(found->lods.size()>=max_lods || std::any_of(found->lods.begin(),found->lods.end(),[&](const auto& other){return other.frame_address==lod.frame_address;}))
                            throw std::invalid_argument("Duplicate aircraft LOD registration");
                        // An override uses that LOD's source-pixel units. Its
                        // actual dimensions are known only to the ROM matcher.
                        native(lod.native_pivot,4096,4096);scale(lod.pixels_per_native);found->lods.push_back(lod);
                    }
                } else throw std::invalid_argument("Unknown aircraft pack record");
                row>>std::ws;if(!row.eof())throw std::invalid_argument("Unexpected trailing aircraft fields");
            } catch(const std::exception& e) {
                throw std::invalid_argument("Aircraft pack line "+std::to_string(line_number)+": "+e.what());
            }
        }
        if(!file.eof() || !header || poses.empty())throw std::invalid_argument("Aircraft pack requires a header and at least one pose");
        return poses;
    }
};
}
