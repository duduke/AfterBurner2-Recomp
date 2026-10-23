#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "frontend/aircraft_pack.hpp"
#include "hwvideo/player_aircraft.hpp"

namespace Frontend {
// Transparent artist art. All placement and ownership are supplied by the
// passive scene, never by frontend guesses about flight state or RAM.
class NativePlayerAircraft {
    struct SurfaceDelete {void operator()(SDL_Surface* p)const{SDL_DestroySurface(p);}};
    struct TextureDelete {void operator()(SDL_Texture* p)const{SDL_DestroyTexture(p);}};
    using Surface=std::unique_ptr<SDL_Surface,SurfaceDelete>;
    using Texture=std::unique_ptr<SDL_Texture,TextureDelete>;
    struct Reflections {
        std::array<Texture,3> textures;
        float width{},height{};
    };
    SDL_Renderer* renderer;
    bool software_renderer;
    Texture texture;
    Reflections texture_reflections;
    SDL_FRect extent{};
    std::string error;
    std::vector<AircraftPose> pack_poses;
    std::vector<Texture> pack_textures;
    std::vector<Reflections> pack_reflections;
    std::vector<unsigned> roots;
    struct RasterAxis {float scale{1};double offset{};};
    RasterAxis raster_x,raster_y;

    void read_raster_transform() {
        float sx{},sy{};int logical_width{},logical_height{};
        SDL_RendererLogicalPresentation mode{};SDL_FRect presentation{};SDL_Rect viewport{};
        if(!SDL_GetRenderScale(renderer,&sx,&sy) ||
           !SDL_GetRenderLogicalPresentation(renderer,&logical_width,&logical_height,&mode) ||
           !SDL_GetRenderLogicalPresentationRect(renderer,&presentation) || !SDL_GetRenderViewport(renderer,&viewport))
            throw std::runtime_error(SDL_GetError());
        if(mode!=SDL_LOGICAL_PRESENTATION_DISABLED) {
            sx*=presentation.w/logical_width;sy*=presentation.h/logical_height;
        } else presentation.x=presentation.y=0;
        if(!std::isfinite(sx) || !std::isfinite(sy) || sx<=0 || sy<=0)
            throw std::invalid_argument("Invalid aircraft renderer scale");
        // SDL's physical viewport starts at floor(viewport*current_scale +
        // logical_offset); presentation values are already output pixels.
        raster_x={sx,std::floor(viewport.x*sx+presentation.x)};
        raster_y={sy,std::floor(viewport.y*sy+presentation.y)};
    }
    static bool quantize_axis(float& destination,float& extent,RasterAxis axis) {
        // A physical pixel belongs to this half-open native strip precisely
        // when its center does. Shared endpoints therefore quantize equally;
        // neither empty cracks nor twice-blended seams can appear.
        const double first=std::ceil(axis.offset+double(destination)*axis.scale-0.5);
        const double last=std::ceil(axis.offset+(double(destination)+extent)*axis.scale-0.5);
        if(first>=last)return false;
        const double begin=(first-axis.offset)/axis.scale,span=(last-first)/axis.scale;
        // Keep the authored source interval: SDL's software copy truncates
        // source rectangles to integer texels. Inverse-adjusting the source
        // after destination quantization can change that crop by one texel,
        // shifting HD detail even though the intended adjustment was tiny.
        // Inverse division can round just below an exact physical endpoint.
        // One upward float ULP prevents SDL's integer copy truncation from
        // dropping that row/column.
        destination=std::nextafter(float(begin),INFINITY);
        extent=std::nextafter(float(span),INFINITY);
        return true;
    }

    Reflections make_reflections(SDL_Surface* rgba) {
        Reflections next;
        if(!software_renderer)return next;
        next.width=float(rgba->w);next.height=float(rgba->h);
        for(unsigned orientation=1;orientation<=3;++orientation) {
            Surface reflected(SDL_DuplicateSurface(rgba));
            if(!reflected || !SDL_FlipSurface(reflected.get(),SDL_FlipMode(orientation)))throw std::runtime_error(SDL_GetError());
            Texture uploaded(SDL_CreateTextureFromSurface(renderer,reflected.get()));
            if(!uploaded || !SDL_SetTextureBlendMode(uploaded.get(),SDL_BLENDMODE_BLEND_PREMULTIPLIED) ||
               !SDL_SetTextureScaleMode(uploaded.get(),SDL_SCALEMODE_LINEAR))throw std::runtime_error(SDL_GetError());
            next.textures[orientation-1]=std::move(uploaded);
        }
        return next;
    }
    bool render_orientation(SDL_Texture* original,const Reflections& variants,
            const SDL_FRect& source,const SDL_FRect& destination,SDL_FlipMode flip) {
        auto sampled_source=source;auto sampled_destination=destination;auto* selected=original;
        // SDL's software CopyEx flip samples the logical one-row ownership
        // strip before output scaling. Cached authored-pixel reflections keep
        // linear HD sampling on the ordinary RenderTexture path instead.
        if(software_renderer && flip!=SDL_FLIP_NONE) {
            if(flip&SDL_FLIP_HORIZONTAL)sampled_source.x=variants.width-source.x-source.w;
            if(flip&SDL_FLIP_VERTICAL)sampled_source.y=variants.height-source.y-source.h;
            selected=variants.textures[unsigned(flip)-1].get();flip=SDL_FLIP_NONE;
        }
        if(software_renderer) {
            if(!quantize_axis(sampled_destination.x,sampled_destination.w,raster_x) ||
               !quantize_axis(sampled_destination.y,sampled_destination.h,raster_y))return true;
        }
        return SDL_RenderTextureRotated(renderer,selected,&sampled_source,&sampled_destination,0,nullptr,flip);
    }

    static bool valid_scene_item(const Presentation::PlayerAircraftDraw& item,
            const Presentation::PlayerAircraftScene& scene) {
        return item.width && item.height && item.width<=32768 && item.height<=32768 &&
            item.top<=item.bottom && item.bottom<224 && item.mask_offset<=scene.visibility.size() &&
            scene.visibility.size()-item.mask_offset>=320*224;
    }
    void draw_pack(const Presentation::PlayerAircraftScene& scene,unsigned viewport_width) {
        const float origin=(viewport_width-320)*0.5f;
        for(const auto& item:scene.draws) {
            const auto found=std::find_if(pack_poses.begin(),pack_poses.end(),[&](const auto& pose){return pose.root_frame_address==item.root_frame_address;});
            if(found==pack_poses.end())continue;
            if(!valid_scene_item(item,scene) || !item.source_width || !item.source_height ||
               item.source_width>4096 || item.source_height>4096)
                throw std::invalid_argument("Invalid registered aircraft rectangle or ownership mask");
            const size_t pose_index=size_t(found-pack_poses.begin());
            auto native_pivot=found->registration.native_pivot;
            auto scale=found->registration.pixels_per_native;
            unsigned base_width=found->base_width,base_height=found->base_height;
            const auto lod=std::find_if(found->lods.begin(),found->lods.end(),[&](const auto& entry){return entry.frame_address==item.frame_address;});
            // The manifest cannot assert ROM LOD dimensions. If an optional
            // override does not fit the actual matched source, use the pose's
            // validated default instead of failing after native detachment.
            if(lod!=found->lods.end() && lod->native_pivot.x<=item.source_width && lod->native_pivot.y<=item.source_height) {
                native_pivot=lod->native_pivot;scale=lod->pixels_per_native;
                base_width=item.source_width;base_height=item.source_height;
            }
            const float sample_x=found->registration.art_pivot.x-native_pivot.x*scale.x;
            const float sample_y=found->registration.art_pivot.y-native_pivot.y*scale.y;
            const float sample_width=base_width*scale.x,sample_height=base_height*scale.y;
            const auto& bounds=found->source;
            // Clip authored source bounds in normalized native coordinates.
            // Reflection is about the ROM frame, including its padding.
            float left=(bounds.x-sample_x)/sample_width,right=(bounds.x+bounds.w-sample_x)/sample_width;
            float top=(bounds.y-sample_y)/sample_height,bottom=(bounds.y+bounds.h-sample_y)/sample_height;
            if(item.flip_x){const float old=left;left=1-right;right=1-old;}
            if(item.flip_y){const float old=top;top=1-bottom;bottom=1-old;}
            const float art_left=item.x+left*item.width,art_right=item.x+right*item.width;
            const float art_top=item.y+top*item.height,art_bottom=item.y+bottom*item.height;
            const auto reflected=SDL_FlipMode((item.flip_x?SDL_FLIP_HORIZONTAL:0)|(item.flip_y?SDL_FLIP_VERTICAL:0));
            for(unsigned row=item.top;row<=item.bottom;++row)for(unsigned column=0;column<320;) {
                if(!scene.visibility[item.mask_offset+row*320+column]){++column;continue;}
                unsigned end=column+1;
                while(end<320 && scene.visibility[item.mask_offset+row*320+end])++end;
                const float x0=std::max(float(column),art_left),x1=std::min(float(end),art_right);
                const float y0=std::max(float(row),art_top),y1=std::min(float(row+1),art_bottom);
                if(x0<x1 && y0<y1) {
                    float u0=(x0-item.x)/item.width,u1=(x1-item.x)/item.width;
                    float v0=(y0-item.y)/item.height,v1=(y1-item.y)/item.height;
                    if(item.flip_x){const float old=u0;u0=1-u1;u1=1-old;}
                    if(item.flip_y){const float old=v0;v0=1-v1;v1=1-old;}
                    const SDL_FRect source{sample_x+u0*sample_width,sample_y+v0*sample_height,
                        (u1-u0)*sample_width,(v1-v0)*sample_height};
                    const SDL_FRect destination{origin+x0,y0,x1-x0,y1-y0};
                    if(!render_orientation(pack_textures[pose_index].get(),pack_reflections[pose_index],source,destination,reflected))
                        throw std::runtime_error(SDL_GetError());
                }
                column=end;
            }
        }
    }
public:
    explicit NativePlayerAircraft(SDL_Renderer* renderer):renderer(renderer),
        software_renderer(SDL_GetRendererName(renderer) && std::string_view(SDL_GetRendererName(renderer))=="software"){}
    bool available()const{return bool(texture) || !pack_textures.empty();}
    const std::string& load_error()const{return error;}
    std::span<const unsigned> supported_roots()const{return roots;}
    std::span<const AircraftPose> poses()const{return pack_poses;}
    std::span<const AircraftPose> registrations()const{return pack_poses;}

    // Stage every image and metadata record before replacing an accepted pack
    // or legacy image. Failed reloads leave both its pixels and supported roots.
    bool load_pack(const std::filesystem::path& path) {
        error.clear();
        try {
            auto descriptions=AircraftPack::read(path);std::vector<Texture> next_textures;
            std::vector<Reflections> next_reflections;
            std::vector<unsigned> next_roots;size_t total_pixels=0;
            for(const auto& pose:descriptions) {
                const auto image_path=AircraftPack::image_file(path,pose.image_path);Surface loaded;
#if SDL_VERSION_ATLEAST(3,4,0)
                loaded.reset(SDL_LoadPNG(image_path.string().c_str()));
#endif
                if(!loaded)loaded.reset(SDL_LoadBMP(image_path.string().c_str()));
                if(!loaded)throw std::invalid_argument("Cannot load aircraft image "+pose.image_path.string()+": "+SDL_GetError());
                if(loaded->w<1 || loaded->h<1 || loaded->w>4096 || loaded->h>4096)
                    throw std::invalid_argument("Aircraft pack images must be at most 4096x4096");
                total_pixels+=size_t(loaded->w)*loaded->h*(software_renderer?4u:1u);
                if(total_pixels>32*1024*1024)throw std::invalid_argument("Aircraft pack exceeds 32 million texture pixels");
                if(pose.source.x+pose.source.w>loaded->w || pose.source.y+pose.source.h>loaded->h)
                    throw std::invalid_argument("Aircraft source rectangle is outside its image");
                Surface rgba(SDL_ConvertSurface(loaded.get(),SDL_PIXELFORMAT_RGBA32));
                if(!rgba)throw std::runtime_error(SDL_GetError());
                bool nonempty=false,transparent=false;
                for(int y=0;y<rgba->h;++y) {
                    auto* row=static_cast<uint8_t*>(rgba->pixels)+y*rgba->pitch;
                    for(int x=0;x<rgba->w;++x) {
                        auto* pixel=row+x*4;const unsigned alpha=pixel[3];
                        transparent|=alpha==0;
                        if(x+1>pose.source.x && x<pose.source.x+pose.source.w && y+1>pose.source.y && y<pose.source.y+pose.source.h) {
                            nonempty|=alpha!=0;
                        }
                        for(unsigned c=0;c<3;++c)pixel[c]=uint8_t((unsigned(pixel[c])*alpha+127u)/255u);
                    }
                }
                if(!nonempty || !transparent)throw std::invalid_argument("Aircraft source requires a nonempty silhouette and transparent background");
                Texture next(SDL_CreateTextureFromSurface(renderer,rgba.get()));
                if(!next || !SDL_SetTextureBlendMode(next.get(),SDL_BLENDMODE_BLEND_PREMULTIPLIED) ||
                   !SDL_SetTextureScaleMode(next.get(),SDL_SCALEMODE_LINEAR))throw std::runtime_error(SDL_GetError());
                auto reflected=make_reflections(rgba.get());
                next_textures.push_back(std::move(next));next_reflections.push_back(std::move(reflected));next_roots.push_back(pose.root_frame_address);
            }
            pack_poses=std::move(descriptions);pack_textures=std::move(next_textures);pack_reflections=std::move(next_reflections);
            roots=std::move(next_roots);texture.reset();texture_reflections={};
            return true;
        } catch(const std::exception& e) {error=e.what();return false;}
    }
    // Validate before replacing the previous accepted image. The alpha bounds
    // remove generator canvas padding without rewriting the authored PNG.
    bool load(const std::filesystem::path& path) {
        if(path.extension()==".aircraft")return load_pack(path);
        error.clear();Surface loaded;
#if SDL_VERSION_ATLEAST(3,4,0)
        loaded.reset(SDL_LoadPNG(path.string().c_str()));
#endif
        if(!loaded)loaded.reset(SDL_LoadBMP(path.string().c_str()));
        if(!loaded){error=SDL_GetError();return false;}
        if(loaded->w<120 || loaded->h<47 || loaded->w>4096 || loaded->h>4096){error="Aircraft art must be 120x47 or larger, at most 4096x4096";return false;}
        Surface rgba(SDL_ConvertSurface(loaded.get(),SDL_PIXELFORMAT_RGBA32));
        if(!rgba){error=SDL_GetError();return false;}
        int left=rgba->w,top=rgba->h,right=-1,bottom=-1;bool transparent=false;
        for(int y=0;y<rgba->h;++y){
            auto* row=static_cast<uint8_t*>(rgba->pixels)+y*rgba->pitch;
            for(int x=0;x<rgba->w;++x){auto* pixel=row+x*4;const unsigned alpha=pixel[3];
                transparent|=alpha==0;
                if(alpha){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
                for(unsigned c=0;c<3;++c)pixel[c]=uint8_t((unsigned(pixel[c])*alpha+127)/255);
            }
        }
        if(right<left || !transparent){error="Aircraft art requires a nonempty silhouette and transparent background";return false;}
        Texture next(SDL_CreateTextureFromSurface(renderer,rgba.get()));
        if(!next || !SDL_SetTextureBlendMode(next.get(),SDL_BLENDMODE_BLEND_PREMULTIPLIED) ||
           !SDL_SetTextureScaleMode(next.get(),SDL_SCALEMODE_LINEAR)){error=SDL_GetError();return false;}
        Reflections next_reflections;std::vector<unsigned> next_roots;
        try {next_reflections=make_reflections(rgba.get());next_roots={0x100a0};}
        catch(const std::exception& e){error=e.what();return false;}
        extent={float(left),float(top),float(right-left+1),float(bottom-top+1)};
        // The legacy 4096x4096 allowance implies at most 64 million cached
        // texels on software (four orientations); packs share a 32M total cap.
        texture=std::move(next);texture_reflections=std::move(next_reflections);
        pack_poses.clear();pack_textures.clear();pack_reflections.clear();roots=std::move(next_roots);return true;
    }

    void draw(const Presentation::PlayerAircraftScene& scene,unsigned viewport_width) {
        if(!available())return;
        if(viewport_width<320 || viewport_width>640 || viewport_width%2)
            throw std::invalid_argument("Invalid aircraft viewport");
        if(software_renderer)read_raster_transform();
        if(!texture){draw_pack(scene,viewport_width);return;}
        const float origin=float(viewport_width-320)/2;
        for(const auto& item:scene.draws){
            if(!item.width || !item.height || item.width>32768 || item.height>32768 ||
               item.top>item.bottom || item.bottom>=224 || item.mask_offset>scene.visibility.size() ||
               scene.visibility.size()-item.mask_offset<320*224)
                throw std::invalid_argument("Invalid aircraft rectangle or ownership mask");
            // Runs of native ownership cells clip HD sampling, retaining full
            // texture resolution inside each cell on high-DPI displays.
            for(unsigned y=item.top;y<=item.bottom;++y)for(unsigned x=0;x<320;){
                const auto index=item.mask_offset+y*320+x;
                if(index>=scene.visibility.size())throw std::invalid_argument("Invalid aircraft ownership mask");
                if(!scene.visibility[index]){++x;continue;}
                unsigned end=x+1;
                while(end<320 && scene.visibility[item.mask_offset+y*320+end])++end;
                float u0=(float(x)-item.x)/item.width,u1=(float(end)-item.x)/item.width;
                float v0=(float(y)-item.y)/item.height,v1=(float(y+1)-item.y)/item.height;
                if(item.flip_x){const auto old=u0;u0=1-u1;u1=1-old;}
                if(item.flip_y){const auto old=v0;v0=1-v1;v1=1-old;}
                const SDL_FRect source{extent.x+u0*extent.w,extent.y+v0*extent.h,(u1-u0)*extent.w,(v1-v0)*extent.h};
                const SDL_FRect destination{origin+x,float(y),float(end-x),1};
                const auto flip=SDL_FlipMode((item.flip_x?SDL_FLIP_HORIZONTAL:0)|(item.flip_y?SDL_FLIP_VERTICAL:0));
                if(!render_orientation(texture.get(),texture_reflections,source,destination,flip))throw std::runtime_error(SDL_GetError());
                x=end;
            }
        }
    }
};
}
