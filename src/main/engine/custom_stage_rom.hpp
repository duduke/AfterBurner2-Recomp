#pragma once

#include "engine/custom_stage_json.hpp"
#include "engine/mainbus.hpp"
#include "engine/shuffle_rom.hpp"
#include "engine/stage_catalog.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CustomStage {

// Deterministic shuffled cycle of donor tanker stages (default: canonical
// refueling stages 3, 9, 11, 16, 19, 21) with no adjacent repeats across cycle
// boundaries. Supports pinning an explicit stage number when validated.
class VariantPicker {
public:
    explicit VariantPicker(uint32_t seed = 0x54414e4bu) : rng(seed) {}

    unsigned pick(const std::vector<unsigned>& variants, unsigned pinned = 0) {
        if (variants.empty())
            throw std::runtime_error("Variant list must not be empty");
        if (pinned) {
            if (std::find(variants.begin(), variants.end(), pinned) == variants.end())
                throw std::runtime_error("Pinned tanker variant is not a valid refueling stage");
            last = pinned;
            return pinned;
        }
        if (variants.size() == 1u) {
            last = variants.front();
            return last;
        }
        if (cursor >= bag.size() || bag.size() != variants.size()) {
            bag = variants;
            do {
                for (unsigned size = unsigned(bag.size()); size > 1u; --size) {
                    const uint32_t threshold = uint32_t(0u - size) % size;
                    uint32_t r{};
                    do { r = uint32_t(rng()); } while (r < threshold);
                    std::swap(bag[size - 1u], bag[r % size]);
                }
            } while (last != 0u && bag.front() == last);
            cursor = 0u;
        }
        last = bag[cursor++];
        return last;
    }

    static std::vector<unsigned> discover_tanker_stages(const Roms& rom) {
        const unsigned count = StageCatalog::count(rom);
        std::vector<unsigned> found;
        for (unsigned s = 1; s <= count; ++s) {
            const auto stage = StageCatalog::get(rom, s);
            uint32_t ev = stage.events;
            for (unsigned n = 0; n < 8192u && ev + 8u <= rom.maincpu.length; ++n, ev += 8u) {
                if (StageCatalog::Detail::word(rom, ev) == 0xffffu) break;
                const uint8_t cmd = rom.maincpu.rom[ev + 2u];
                if (cmd == 14u) { found.push_back(s); break; }
                if (cmd == 12u) break;
            }
        }
        if (found.empty())
            return {3u, 9u, 11u, 16u, 19u, 21u};
        return found;
    }

private:
    std::mt19937 rng;
    std::vector<unsigned> bag;
    size_t cursor{};
    unsigned last{};
};

namespace Detail {

inline std::array<uint8_t, 8> encode_hud_title(std::string_view name) {
    std::array<uint8_t, 8> out;
    out.fill(0x20u); // ASCII space in the ROM tilemap font
    size_t pos = 0;
    for (unsigned char ch : name) {
        if (pos >= out.size()) break;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<unsigned char>(ch - 'a' + 'A');
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '/' || ch == '-')
            out[pos++] = ch;
    }
    return out;
}

inline std::array<uint8_t, 32> encode_stage_banner(std::string_view name) {
    std::array<uint8_t, 32> out;
    out.fill(0x20u);
    size_t pos = 0;
    for (unsigned char ch : name) {
        if (pos >= out.size()) break;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<unsigned char>(ch - 'a' + 'A');
        if (ch >= 0x20u && ch <= 0x5au)
            out[pos++] = ch;
    }
    return out;
}

inline bool stage_has_command18(const Roms& canonical, unsigned stage_number) {
    if (stage_number <= 1u || stage_number > StageCatalog::count(canonical))
        return false;
    uint32_t event = StageCatalog::get(canonical, stage_number - 1u).events;
    for (unsigned record = 0; record < 8192u; ++record, event += 8u) {
        StageCatalog::Detail::range(canonical, event, 8u);
        if (StageCatalog::Detail::word(canonical, event) == 0xffffu) break;
        const uint8_t command = canonical.maincpu.rom[event + 2u];
        if (command == 18u) return true;
        if (command == 12u) break;
    }
    return false;
}

} // namespace Detail

// Compiles a declarative CustomStage::Definition into an isolated, owned Roms
// image in expanded upper Main-CPU ROM (0x40000..0x80000). Canonical ROM bytes
// and all 68000 instructions remain untouched.
class RomView {
public:
    explicit RomView(const Roms& canonical, const Definition& definition)
        : def(definition) {
        const unsigned count = StageCatalog::count(canonical);
        if (!def.slot || def.slot >= count)
            throw std::runtime_error("Custom stage slot must be between 1 and count - 1");
        const unsigned donor = def.donor_stage();
        if (!donor || donor > count)
            throw std::runtime_error("Custom stage donor stage out of range");
        if (def.enemy_stage && (!*def.enemy_stage || *def.enemy_stage > count))
            throw std::runtime_error("Custom stage enemy_stage out of range");
        if (def.scenery_stage && (!*def.scenery_stage || *def.scenery_stage > count))
            throw std::runtime_error("Custom stage scenery_stage out of range");
        if (canonical.maincpu.length > 0x40000u)
            throw std::runtime_error("CustomStage::RomView requires an unmodified canonical main ROM");

        Shuffle::Detail::clone(canonical.maincpu, owned.maincpu, 0x80000u);
        for (auto member : {&Roms::subcpu, &Roms::tiles, &Roms::sprites,
                            &Roms::road, &Roms::soundcpu, &Roms::pcm})
            Shuffle::Detail::clone(canonical.*member, owned.*member);

        const unsigned target_pos = def.slot - 1u;
        const unsigned donor_pos = donor - 1u;

        // 1. Copy donor setup/road/curve/color words into target_pos, then apply overrides.
        for (const uint32_t base : {0x32196u, 0x32324u, 0x32452u, 0x30d54u}) {
            StageCatalog::Detail::range(canonical, base, count * 2u);
            std::copy_n(canonical.maincpu.rom + base + donor_pos * 2u, 2u,
                        owned.maincpu.rom + base + target_pos * 2u);
        }
        if (def.road_setup)
            Shuffle::Detail::write16(owned, 0x32196u + target_pos * 2u, *def.road_setup);
        if (def.sky_setup)
            Shuffle::Detail::write16(owned, 0x32324u + target_pos * 2u, *def.sky_setup);
        if (def.curve_setup)
            Shuffle::Detail::write16(owned, 0x32452u + target_pos * 2u, *def.curve_setup);

        for (const uint32_t base : {0x32352u, 0x32224u}) {
            for (unsigned bank = 0; bank < 4u; ++bank) {
                const uint32_t table = base + bank * 0x40u;
                StageCatalog::Detail::range(canonical, table, count * 2u);
                std::copy_n(canonical.maincpu.rom + table + donor_pos * 2u, 2u,
                            owned.maincpu.rom + table + target_pos * 2u);
            }
        }

        // 2. Music table ($2B604): preserve inherited song continuity across all positions.
        StageCatalog::Detail::range(canonical, 0x2b604u, count);
        auto resolve_canonical_music = [&](unsigned pos) {
            while (pos && canonical.maincpu.rom[0x2b604u + pos] == 0x80u)
                --pos;
            return canonical.maincpu.rom[0x2b604u + pos];
        };
        uint8_t previous_music = 0x80u;
        for (unsigned pos = 0; pos < count; ++pos) {
            const unsigned src = (pos == target_pos) ? donor_pos : pos;
            uint8_t music = resolve_canonical_music(src);
            uint8_t command = canonical.maincpu.rom[0x2b604u + src];
            if (pos == target_pos && def.music_cmd && *def.music_cmd != 0x80u) {
                music = *def.music_cmd;
                command = *def.music_cmd;
            } else if (command == 0x80u && (!pos || music != previous_music)) {
                command = music;
            }
            owned.maincpu.rom[0x2b604u + pos] = command;
            previous_music = music;
        }

        // 3. Stage HUD title ($2B6AC, 8 bytes) and banner ($2EA16, 32 bytes).
        StageCatalog::Detail::range(canonical, 0x2b6acu, count * 8u);
        StageCatalog::Detail::range(canonical, 0x2ea16u, count * 32u);
        const auto title8 = Detail::encode_hud_title(def.name);
        const auto banner32 = Detail::encode_stage_banner(def.name);
        std::copy(title8.begin(), title8.end(), owned.maincpu.rom + 0x2b6acu + target_pos * 8u);
        std::copy(banner32.begin(), banner32.end(), owned.maincpu.rom + 0x2ea16u + target_pos * 32u);

        // 4. Relocate all 5 indexed palette blocks into upper ROM (0x40000+) before
        // touching cold-start aliases, then apply custom ocean/atmosphere colors.
        uint32_t cursor = 0x40000u;
        constexpr std::array<std::pair<uint32_t, unsigned>, 5> palette_tables{{
            {0x140dau, 64u}, {0x1413au, 256u}, {0x14196u, 256u},
            {0x141f2u, 256u}, {0x1424eu, 256u}
        }};
        for (const auto& [base, bytes] : palette_tables) {
            for (unsigned pos = 0; pos < count; ++pos) {
                const unsigned src = (pos == target_pos) ? donor_pos : pos;
                const uint32_t source_addr = StageCatalog::Detail::pointer(canonical, base + src * 4u);
                const uint32_t relocated = append(canonical, source_addr, bytes, cursor);
                if (pos == target_pos && def.custom_ocean && base == 0x140dau) {
                    // Populate the 14-word horizon/sea gradient at offset +34 (bytes 34..61)
                    // and primary sky/ground words so the Sub-CPU road generator uses them.
                    for (unsigned w = 0; w < 14u; ++w) {
                        const RGBColor c = (w < 7u)
                            ? def.ocean.sky_at(double(w) / 6.0)
                            : def.ocean.sea_at(double(w - 7u) / 6.0);
                        Shuffle::Detail::write16(owned, relocated + 34u + w * 2u, c.rgb555());
                    }
                }
                Shuffle::Detail::write32(owned, base + pos * 4u, relocated);
            }
        }

        // Update cold-start palette aliases from position 0's relocated blocks.
        copy_owned(0x2bc60u, StageCatalog::Detail::pointer(owned, 0x140dau) + 34u, 28u);
        copy_owned(0x2cff4u, StageCatalog::Detail::pointer(owned, 0x1413au), 256u);
        copy_owned(0x2dbf4u, StageCatalog::Detail::pointer(owned, 0x14196u), 256u);
        copy_owned(0x2bf60u, StageCatalog::Detail::pointer(owned, 0x141f2u) + 7u * 32u + 2u, 28u);

        // 5. Compile stage descriptors and 8-byte event programs into upper ROM.
        alternate.assign(count, false);
        for (unsigned pos = 0; pos < count; ++pos) {
            const unsigned src_stage = (pos == target_pos) ? donor : (pos + 1u);
            const auto source = StageCatalog::get(canonical, src_stage);
            alternate[pos] = Detail::stage_has_command18(canonical, src_stage);

            const uint32_t descriptor = append(canonical, source.descriptor, 20u, cursor);
            if (pos == target_pos && def.custom_ocean) {
                // Clone the 24-byte alternate/primary sky-ground descriptor block
                // referenced by descriptor+0 and descriptor+4 and install ocean RGB555.
                const uint32_t primary_pal = StageCatalog::Detail::pointer(canonical, source.descriptor);
                if (primary_pal + 8u <= canonical.maincpu.length) {
                    const uint32_t custom_pal = append(canonical, primary_pal, 8u, cursor);
                    const uint16_t sky555 = def.ocean.sky.rgb555();
                    const uint16_t sea555 = def.ocean.water.rgb555();
                    Shuffle::Detail::write16(owned, custom_pal, sky555);
                    Shuffle::Detail::write16(owned, custom_pal + 2u, sky555);
                    Shuffle::Detail::write16(owned, custom_pal + 4u, sea555);
                    Shuffle::Detail::write16(owned, custom_pal + 6u, sea555);
                    Shuffle::Detail::write32(owned, descriptor, custom_pal);
                }
            }

            if (pos == target_pos) {
                if (def.scenery_stage) {
                    const auto sc_donor = StageCatalog::get(canonical, *def.scenery_stage);
                    const uint32_t sc_ptr = StageCatalog::Detail::pointer(canonical, sc_donor.descriptor + 12u);
                    Shuffle::Detail::write32(owned, descriptor + 12u, sc_ptr);
                }
                const unsigned enemy_donor_stage = def.enemy_stage ? *def.enemy_stage : donor;
                const auto en_donor = StageCatalog::get(canonical, enemy_donor_stage);
                const uint32_t donor_enemies = StageCatalog::Detail::pointer(canonical, en_donor.descriptor + 8u);
                if (!def.custom_enemy_waves.empty() || def.replace_enemy_waves) {
                    const uint32_t enemies_addr = cursor;
                    compile_target_enemy_waves(canonical, donor_enemies, cursor);
                    Shuffle::Detail::write32(owned, descriptor + 8u, enemies_addr);
                } else if (def.enemy_stage) {
                    Shuffle::Detail::write32(owned, descriptor + 8u, donor_enemies);
                }
            }

            const uint32_t events_addr = cursor;
            if (pos == target_pos) {
                compile_target_events(canonical, source.events, cursor);
            } else {
                copy_filtered_events(canonical, source.events, cursor);
            }

            // Use the slot's own canonical selector so all 23 selectors remain unique
            // even when a custom stage borrows its content from another donor slot!
            const uint8_t slot_selector = StageCatalog::get(canonical, pos + 1u).selector;
            owned.maincpu.rom[0x32482u + pos] = slot_selector;
            Shuffle::Detail::write32(owned, 0x3249au + uint32_t(slot_selector) * 4u, descriptor);
            Shuffle::Detail::write32(owned, descriptor + 16u, events_addr);
        }
    }

    RomView(const RomView&) = delete;
    RomView& operator=(const RomView&) = delete;
    RomView(RomView&&) = delete;
    RomView& operator=(RomView&&) = delete;

    Roms& rom() { return owned; }
    const Roms& rom() const { return owned; }
    const Definition& definition() const { return def; }

    bool needs_alternate_palette(unsigned position) const {
        if (position >= alternate.size())
            throw std::out_of_range("CustomStage position out of range");
        return alternate[position];
    }

    uint32_t compiled_events_address() const {
        return StageCatalog::get(owned, def.slot).events;
    }

    uint32_t compiled_enemy_waves_address() const {
        const auto stage = StageCatalog::get(owned, def.slot);
        return StageCatalog::Detail::pointer(owned, stage.descriptor + 8u);
    }

private:
    Definition def;
    Roms owned;
    std::vector<bool> alternate;

    void copy_owned(uint32_t destination, uint32_t source, unsigned bytes) {
        StageCatalog::Detail::range(owned, source, bytes);
        StageCatalog::Detail::range(owned, destination, bytes);
        std::copy_n(owned.maincpu.rom + source, bytes, owned.maincpu.rom + destination);
    }

    uint32_t append(const Roms& source_rom, uint32_t address, unsigned bytes, uint32_t& cursor) {
        StageCatalog::Detail::range(source_rom, address, bytes);
        StageCatalog::Detail::range(owned, cursor, bytes);
        const uint32_t result = cursor;
        std::copy_n(source_rom.maincpu.rom + address, bytes, owned.maincpu.rom + cursor);
        cursor += bytes;
        return result;
    }

    void emit_record(const EventRecord& rec, uint32_t& cursor) {
        StageCatalog::Detail::range(owned, cursor, 8u);
        const auto bytes = rec.encode();
        std::copy(bytes.begin(), bytes.end(), owned.maincpu.rom + cursor);
        cursor += 8u;
    }

    void copy_filtered_events(const Roms& canonical, uint32_t event, uint32_t& cursor) {
        bool terminal = false;
        for (unsigned record = 0; record < 8192u; ++record, event += 8u) {
            StageCatalog::Detail::range(canonical, event, 8u);
            if (StageCatalog::Detail::word(canonical, event) == 0xffffu) break;
            const uint8_t command = canonical.maincpu.rom[event + 2u];
            if (command != 18u)
                append(canonical, event, 8u, cursor);
            if (command == 12u) {
                if (StageCatalog::Detail::word(canonical, event + 8u) != 0xffffu)
                    throw std::runtime_error("Stage terminal event lacks its sentinel");
                append(canonical, event + 8u, 8u, cursor);
                terminal = true;
                break;
            }
        }
        if (!terminal)
            throw std::runtime_error("Canonical stage has no terminal event");
    }

    void compile_target_events(const Roms& canonical, uint32_t donor_events, uint32_t& cursor) {
        std::vector<EventRecord> stream;
        EventRecord terminal_record{0x0100u, 12u, {0, 0, 0, 0, 0}};
        EventRecord sentinel_record{0xffffu, 0u, {0, 0, 0, 0, 0}};
        bool found_donor_terminal = false;

        for (unsigned record = 0; record < 8192u; ++record, donor_events += 8u) {
            StageCatalog::Detail::range(canonical, donor_events, 8u);
            const uint16_t word0 = StageCatalog::Detail::word(canonical, donor_events);
            if (word0 == 0xffffu) break;
            EventRecord rec;
            rec.tick = word0;
            rec.command = canonical.maincpu.rom[donor_events + 2u];
            for (unsigned i = 0; i < 5u; ++i)
                rec.params[i] = canonical.maincpu.rom[donor_events + 3u + i];
            if (rec.command == 12u) {
                if (StageCatalog::Detail::word(canonical, donor_events + 8u) != 0xffffu)
                    throw std::runtime_error("Donor terminal event lacks its sentinel");
                terminal_record = rec;
                for (unsigned i = 0; i < 5u; ++i)
                    sentinel_record.params[i] = canonical.maincpu.rom[donor_events + 11u + i];
                sentinel_record.command = canonical.maincpu.rom[donor_events + 10u];
                found_donor_terminal = true;
                break;
            }
            if (!def.replace_events && rec.command != 18u) {
                rec.validate_control();
                stream.push_back(rec);
            }
        }
        if (!found_donor_terminal)
            throw std::runtime_error("Donor stage has no terminal event");

        bool explicit_terminal = false;
        for (const auto& custom : def.custom_events) {
            custom.validate_control();
            if (custom.command == 12u) {
                terminal_record = custom;
                explicit_terminal = true;
            } else {
                stream.push_back(custom);
            }
        }

        std::stable_sort(stream.begin(), stream.end(), [](const EventRecord& a, const EventRecord& b) {
            return a.tick < b.tick;
        });

        if (!explicit_terminal && !stream.empty() && stream.back().tick >= terminal_record.tick) {
            const uint32_t next_tick = std::min<uint32_t>(0xfffeu, uint32_t(stream.back().tick) + 16u);
            terminal_record.tick = static_cast<uint16_t>(next_tick);
        }

        for (const auto& rec : stream)
            emit_record(rec, cursor);
        emit_record(terminal_record, cursor);
        emit_record(sentinel_record, cursor);
    }

    void compile_target_enemy_waves(const Roms& canonical, uint32_t donor_waves, uint32_t& cursor) {
        std::vector<EventRecord> stream;
        if (!def.replace_enemy_waves && donor_waves + 8u <= canonical.maincpu.length) {
            for (unsigned record = 0; record < 8192u && donor_waves + 8u <= canonical.maincpu.length; ++record, donor_waves += 8u) {
                const uint16_t word0 = StageCatalog::Detail::word(canonical, donor_waves);
                if (word0 == 0xffffu) break;
                EventRecord rec;
                rec.tick = word0;
                rec.command = canonical.maincpu.rom[donor_waves + 2u];
                for (unsigned i = 0; i < 5u; ++i)
                    rec.params[i] = canonical.maincpu.rom[donor_waves + 3u + i];
                stream.push_back(rec);
            }
        }
        for (const auto& custom : def.custom_enemy_waves) {
            custom.validate_enemy_wave();
            const uint32_t form_entry = 0x3f000u + uint32_t(custom.params[0]) * 8u;
            StageCatalog::Detail::range(canonical, form_entry, 8u);
            const uint32_t wave_ptr = StageCatalog::Detail::pointer(canonical, form_entry);
            StageCatalog::Detail::range(canonical, wave_ptr, 4u);
            stream.push_back(custom);
        }
        std::stable_sort(stream.begin(), stream.end(), [](const EventRecord& a, const EventRecord& b) {
            return a.tick < b.tick;
        });
        for (const auto& rec : stream)
            emit_record(rec, cursor);
        emit_record(EventRecord{0xffffu, 0u, {0, 0, 0, 0, 0}}, cursor);
    }
};

} // namespace CustomStage
