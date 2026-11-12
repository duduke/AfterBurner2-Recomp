#pragma once

#include "engine/custom_stage_json.hpp"
#include "../../third_party/miniz/miniz.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// External content for custom stages. A stage can read files from any number of
// mounted folders and ZIP archives, so its art and audio are not limited to the
// arcade ROM set. Nothing here needs SDL; image and audio decoding belong to the
// frontend, which hands decoded data back through the ImageDecoder callback.
namespace CustomStage {

inline std::filesystem::path utf8_path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

inline std::string path_utf8(const std::filesystem::path& value) {
    const auto text = value.generic_u8string();
    return std::string(text.begin(), text.end());
}

// Asset references are always relative to a mount. Absolute paths and parent
// traversal are rejected so a stage cannot read outside the content it mounts.
// Backslashes are accepted because Windows authors will naturally type them.
inline std::string normalize_asset_path(std::string_view raw) {
    std::string path(raw);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) path.erase(0, 2);
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos)
        throw std::runtime_error("Unsafe asset path: " + std::string(raw));
    std::string part;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') {
            const auto c = static_cast<unsigned char>(path[i]);
            if (c < 32 || c == 127) throw std::runtime_error("Unsafe asset path: " + std::string(raw));
            part += path[i];
            continue;
        }
        if (part.empty() || part == "." || part == "..")
            throw std::runtime_error("Unsafe asset path: " + std::string(raw));
        part.clear();
    }
    return path;
}

inline std::string lowercase_ascii(std::string value) {
    for (auto& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

class AssetMounts {
public:
    static constexpr uint64_t file_limit = 256ull * 1024 * 1024;
    static constexpr uint64_t archive_limit = 1024ull * 1024 * 1024;

    // Mounts are searched in the order they were added: the first match wins.
    void add(const std::string& location) { mounts.push_back(open(location)); }

    bool empty() const { return mounts.empty(); }
    size_t size() const { return mounts.size(); }
    std::string label(size_t index) const { return mounts.at(index).label; }

    bool exists(const std::string& relative) const {
        const auto path = normalize_asset_path(relative);
        return std::any_of(mounts.begin(), mounts.end(), [&](const Mount& m) { return locate(m, path); });
    }

    std::optional<std::vector<uint8_t>> read(const std::string& relative) const {
        const auto path = normalize_asset_path(relative);
        for (const auto& mount : mounts)
            if (auto bytes = read_from(mount, path)) return bytes;
        return std::nullopt;
    }

    std::vector<uint8_t> require(const std::string& relative) const {
        if (auto bytes = read(relative)) return std::move(*bytes);
        throw std::runtime_error("Asset not found: " + relative + " (searched " + search_list() + ")");
    }

    std::string search_list() const {
        std::string out;
        for (const auto& mount : mounts) {
            if (!out.empty()) out += ", ";
            out += mount.label;
        }
        return out.empty() ? std::string("no asset locations") : out;
    }

private:
    struct Archive {
        mz_zip_archive zip{};
        std::vector<uint8_t> bytes;
        std::map<std::string, mz_uint> entries; // lowercase path -> file index
        bool open{};
        ~Archive() { if (open) mz_zip_reader_end(&zip); }
    };
    struct Mount {
        std::string label;
        std::filesystem::path directory;
        std::shared_ptr<Archive> archive;
    };
    std::vector<Mount> mounts;

    static std::vector<uint8_t> read_file(const std::filesystem::path& path, uint64_t limit) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) throw std::runtime_error("Cannot read " + path_utf8(path));
        if (size > limit) throw std::runtime_error("File too large: " + path_utf8(path));
        std::vector<uint8_t> bytes(size);
        std::ifstream in(path, std::ios::binary);
        if (!in || (size && !in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))))
            throw std::runtime_error("Cannot read " + path_utf8(path));
        return bytes;
    }

    static Mount open(const std::string& location) {
        const auto path = utf8_path(location);
        std::error_code error;
        Mount mount;
        mount.label = path_utf8(path.filename().empty() ? path.parent_path().filename() : path.filename());
        if (std::filesystem::is_directory(path, error)) {
            mount.directory = std::filesystem::weakly_canonical(path, error);
            if (error) throw std::runtime_error("Cannot open asset folder: " + location);
            return mount;
        }
        if (!std::filesystem::is_regular_file(path, error))
            throw std::runtime_error("Asset location not found: " + location);
        auto archive = std::make_shared<Archive>();
        archive->bytes = read_file(path, archive_limit);
        // Reading through std::filesystem keeps non-ASCII Windows paths working.
        if (!mz_zip_reader_init_mem(&archive->zip, archive->bytes.data(), archive->bytes.size(), 0))
            throw std::runtime_error("Not a readable ZIP archive: " + location);
        archive->open = true;
        const auto count = mz_zip_reader_get_num_files(&archive->zip);
        if (count > 65536) throw std::runtime_error("ZIP has too many files: " + location);
        for (mz_uint i = 0; i < count; ++i) {
            if (mz_zip_reader_is_file_a_directory(&archive->zip, i)) continue;
            const auto length = mz_zip_reader_get_filename(&archive->zip, i, nullptr, 0);
            if (!length || length > 4096) continue;
            std::string name(length, '\0');
            mz_zip_reader_get_filename(&archive->zip, i, name.data(), length);
            name.pop_back();
            // A hostile entry name cannot escape: it is only ever a lookup key.
            std::replace(name.begin(), name.end(), '\\', '/');
            archive->entries.try_emplace(lowercase_ascii(std::move(name)), i);
        }
        mount.archive = std::move(archive);
        return mount;
    }

    static std::optional<std::filesystem::path> directory_file(const Mount& mount, const std::string& path) {
        std::error_code error;
        const auto real = std::filesystem::weakly_canonical(mount.directory / utf8_path(path), error);
        if (error || !std::filesystem::is_regular_file(real, error)) return std::nullopt;
        // A symlink inside the mount must not reach outside it.
        const auto [root_end, unused] = std::mismatch(mount.directory.begin(), mount.directory.end(), real.begin(), real.end());
        (void)unused;
        if (root_end != mount.directory.end()) return std::nullopt;
        return real;
    }

    static bool locate(const Mount& mount, const std::string& path) {
        if (mount.archive) return mount.archive->entries.contains(lowercase_ascii(path));
        return directory_file(mount, path).has_value();
    }

    static std::optional<std::vector<uint8_t>> read_from(const Mount& mount, const std::string& path) {
        if (!mount.archive) {
            const auto file = directory_file(mount, path);
            if (!file) return std::nullopt;
            return read_file(*file, file_limit);
        }
        const auto entry = mount.archive->entries.find(lowercase_ascii(path));
        if (entry == mount.archive->entries.end()) return std::nullopt;
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&mount.archive->zip, entry->second, &stat) || stat.m_uncomp_size > file_limit)
            throw std::runtime_error("Oversized or invalid ZIP entry: " + path);
        std::vector<uint8_t> bytes(static_cast<size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&mount.archive->zip, entry->second, bytes.data(), bytes.size(), 0))
            throw std::runtime_error("Corrupt ZIP entry: " + path);
        return bytes;
    }
};

using ImageDecoder = std::function<DecodedImage(std::span<const uint8_t>, const std::string&)>;

// A stage definition together with everything it loaded from outside the ROMs.
// `source` may be a preset name, a stage .json, a folder holding stage.json, or a
// .zip holding stage.json. `extra_assets` are the folders/ZIPs chosen when loading.
struct StagePackage {
    std::string source;
    std::vector<std::string> extra_assets;
    Definition definition;
    AssetMounts mounts;
    std::map<std::string, std::shared_ptr<const DecodedImage>> images;
    std::shared_ptr<const std::vector<uint8_t>> music_bytes;

    std::shared_ptr<const DecodedImage> image(const std::string& asset) const {
        const auto found = images.find(lowercase_ascii(normalize_asset_path(asset)));
        return found == images.end() ? nullptr : found->second;
    }

    static std::shared_ptr<StagePackage> load(const std::string& source,
                                              const std::vector<std::string>& extra_assets,
                                              const ImageDecoder& decode_image) {
        auto package = std::make_shared<StagePackage>();
        package->source = source;
        package->extra_assets = extra_assets;
        // The most recently added pack has priority, so a pack can reskin a stage.
        for (auto extra = extra_assets.rbegin(); extra != extra_assets.rend(); ++extra)
            package->mounts.add(*extra);
        package->read_definition();
        package->load_assets(decode_image);
        return package;
    }

    // Stage folders/ZIPs a menu can list directly, without a file dialog.
    static bool looks_like_stage(const std::filesystem::path& path) {
        std::error_code error;
        if (std::filesystem::is_directory(path, error))
            return std::filesystem::is_regular_file(path / "stage.json", error);
        const auto extension = lowercase_ascii(path_utf8(path.extension()));
        const auto name = lowercase_ascii(path_utf8(path.filename()));
        return std::filesystem::is_regular_file(path, error) && (extension == ".zip" ||
            (extension == ".json" && !name.ends_with(".schema.json")));
    }

    static std::vector<std::string> discover(const std::vector<std::filesystem::path>& roots) {
        std::vector<std::string> found;
        for (const auto& root : roots) {
            std::error_code error;
            for (auto it = std::filesystem::directory_iterator(root, error);
                 !error && it != std::filesystem::directory_iterator(); it.increment(error))
                if (looks_like_stage(it->path())) found.push_back(path_utf8(it->path()));
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            return lowercase_ascii(a) < lowercase_ascii(b);
        });
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

private:
    void read_definition() {
        const auto path = utf8_path(source);
        std::error_code error;
        if (std::filesystem::is_directory(path, error)) {
            const auto file = stage_file_in_directory(path);
            definition = Definition::from_json(parse_named(file));
            mount_home(file.parent_path());
            return;
        }
        if (std::filesystem::is_regular_file(path, error)) {
            if (lowercase_ascii(path_utf8(path.extension())) == ".zip") {
                read_zip_stage();
                return;
            }
            definition = Definition::from_json(parse_named(path));
            mount_home(path.parent_path());
            return;
        }
        try {
            definition = Definition::preset_by_name(source);
        } catch (const std::exception&) {
            throw std::runtime_error("Custom stage not found (not a file, folder or preset name): " + source);
        }
    }

    static Json parse_named(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open custom stage: " + path_utf8(file));
        try {
            return Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
        } catch (const std::exception& e) {
            throw std::runtime_error(path_utf8(file.filename()) + ": " + e.what());
        }
    }

    static std::filesystem::path stage_file_in_directory(const std::filesystem::path& directory) {
        const auto preferred = directory / "stage.json";
        std::error_code error;
        if (std::filesystem::is_regular_file(preferred, error)) return preferred;
        throw std::runtime_error("Folder has no stage.json: " + path_utf8(directory));
    }

    void read_zip_stage() {
        // The archive is the stage's home, searched after any chosen extras.
        // Its stage.json may sit at the root or inside one top-level folder.
        AssetMounts home;
        home.add(source);
        std::string prefix;
        auto bytes = home.read("stage.json");
        if (!bytes) {
            const auto inner = single_top_level_folder(source);
            if (inner.empty()) throw std::runtime_error("ZIP has no stage.json: " + source);
            prefix = inner + "/";
            bytes = home.read(prefix + "stage.json");
            if (!bytes) throw std::runtime_error("ZIP has no stage.json: " + source);
        }
        definition = Definition::from_json(Json::parse(std::string_view(
            reinterpret_cast<const char*>(bytes->data()), bytes->size())));
        if (!definition.asset_packs.empty())
            throw std::runtime_error("A stage inside a ZIP cannot reference asset_packs outside it");
        zip_prefix = prefix;
        mounts.add(source);
    }

    static std::string single_top_level_folder(const std::string& location) {
        std::ifstream in(utf8_path(location), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) return {};
        std::string folder;
        bool unique = true;
        const auto count = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < count && unique; ++i) {
            const auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
            if (!length) continue;
            std::string name(length, '\0');
            mz_zip_reader_get_filename(&zip, i, name.data(), length);
            name.pop_back();
            const auto slash = name.find('/');
            if (slash == std::string::npos) { unique = false; break; }
            const auto top = name.substr(0, slash);
            if (folder.empty()) folder = top;
            else if (folder != top) unique = false;
        }
        mz_zip_reader_end(&zip);
        return unique ? folder : std::string{};
    }

    void mount_home(const std::filesystem::path& home) {
        // Packs named by the stage file resolve against the stage's own folder.
        for (const auto& pack : definition.asset_packs) {
            const auto path = utf8_path(pack);
            if (path.is_absolute())
                throw std::runtime_error("asset_packs must be relative to the stage file: " + pack);
            mounts.add(path_utf8(home / path));
        }
        mounts.add(path_utf8(home.empty() ? std::filesystem::path(".") : home));
    }

    std::string zip_prefix;

    std::vector<uint8_t> fetch(const std::string& asset) const {
        const auto path = normalize_asset_path(asset);
        // Extras override the stage's own files whatever folder the ZIP nests in.
        if (auto bytes = mounts.read(path)) return std::move(*bytes);
        if (!zip_prefix.empty())
            if (auto bytes = mounts.read(zip_prefix + path)) return std::move(*bytes);
        throw std::runtime_error("Asset not found: " + asset + " (searched " + mounts.search_list() + ")");
    }

    std::shared_ptr<const DecodedImage> decode(const std::string& asset, const ImageDecoder& decode_image) {
        auto& slot = images[lowercase_ascii(normalize_asset_path(asset))];
        if (!slot) {
            const auto bytes = fetch(asset);
            if (!decode_image) throw std::runtime_error("No image decoder available for " + asset);
            auto image = std::make_shared<DecodedImage>(decode_image(bytes, asset));
            if (!image->width || !image->height ||
                image->rgba.size() != size_t(image->width) * image->height * 4u)
                throw std::runtime_error("Invalid decoded image: " + asset);
            slot = std::move(image);
        }
        return slot;
    }

    void load_assets(const ImageDecoder& decode_image) {
        for (const auto& layer : definition.layers)
            for (const auto& element : layer.elements)
                if (element.type == LayerElement::Type::image)
                    decode(element.asset, decode_image);
        if (definition.backdrop)
            decode(definition.backdrop->asset, decode_image);
        if (definition.carrier_art) {
            decode(definition.carrier_art->deck, decode_image);
            for (const auto* extra : {&definition.carrier_art->island, &definition.carrier_art->jet, &definition.carrier_art->water})
                if (!extra->empty()) decode(*extra, decode_image);
        }
        if (definition.boss) {
            decode(definition.boss->hull_asset, decode_image);
            for (const auto& part : definition.boss->parts) {
                decode(part.asset, decode_image);
                if (!part.broken_asset.empty()) decode(part.broken_asset, decode_image);
            }
        }
        for (auto& sprite : definition.sprites)
            sprite.image = decode(sprite.asset, decode_image);
        for (auto& patch : definition.rom_patches)
            patch.data = std::make_shared<const std::vector<uint8_t>>(fetch(patch.asset));
        if (definition.music_file)
            music_bytes = std::make_shared<const std::vector<uint8_t>>(fetch(*definition.music_file));
    }
};

} // namespace CustomStage
