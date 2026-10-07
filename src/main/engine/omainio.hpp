#pragma once
#include <array>
#include <utility>
#include <vector>
#include <optional>
#include "engine/mainbus.hpp"
#include "hwmath/xboard_cpu_visibility.hpp"

// Physical endpoints from segaxbd.cpp. CPU transaction times and interrupt
// entry boundaries remain the caller's responsibility; this schedules no CPU.
// The owner must outlive the bus callbacks and destroy this before the bus.
class OMainIO {
public:
    explicit OMainIO(MainBus& bus) : board(bus.compare_timer), cpu(board) {
        inputs.fill(0x10); // Unconnected ADC ports in the X-Board driver.
        inputs[0]=inputs[1]=inputs[2]=inputs[3]=inputs[4]=0x80; // Motor feedback lanes 3/4.
        board.adc.analog=[this] { return inputs[(mux>>2)&7]; };
        bus.endpoints.adc_read=[this] { return board.adc.read(board.now()); };
        bus.endpoints.adc_write=[this](uint8_t) { board.adc.write(transaction_ns.value_or(board.now())); };
        bus.io[0].output=[this](unsigned port,uint8_t data,uint8_t) {
            if(port==2) mux=data;
        };
        bus.io[1].input=[this](unsigned port) -> uint8_t {
            return port==0 ? buttons : 0xff;
        };
        bus.compare_timer.sound_command=[this](uint8_t command) {
            sound_commands.push_back(command);
        };
    }
    OMainIO(const OMainIO&)=delete;
    OMainIO& operator=(const OMainIO&)=delete;

    // CPU completion/bus frontiers deliver pending board events. The ADC
    // strobe occurs at the transaction time even when delivery still trails
    // it after an internal CPU overrun. Unported frontiers remain inputs.
    void before_bus(uint64_t ns,uint64_t previous_phase_end,
                    uint16_t completed_opcode=0,uint16_t shift_source=0) {
        cpu.resume(previous_phase_end,completed_opcode,shift_source);
        cpu.before_bus(ns);
        transaction_ns=ns;
    }

    // Native host uses three keyboard positions. Preserve physical endpoints;
    // $B0AC applies the gameplay clamps AFTER conversion. Y is reversed:
    // native Up=-1 maps to the physical ADC high endpoint.
    void handle_input(int x,int y,bool vulcan,bool missile,int throttle) {
        inputs[0]=x<0 ? 0x20 : x>0 ? 0xe0 : 0x80;
        inputs[1]=y<0 ? 0xc0 : y>0 ? 0x40 : 0x80;
        inputs[2]=throttle<=0 ? 0 : throttle>=2 ? 0xff : 0x80;
        buttons=static_cast<uint8_t>(0xff & ~(vulcan ? 0x10 : 0) & ~(missile ? 0x20 : 0));
    }
    // External physical input boundary; call advance_before(ns) first for
    // MAME's line-224 notifier, then change inputs before delivering that edge.
    void set_inputs(const std::array<uint8_t,8>& values) { inputs=values; }
    void set_buttons(uint8_t value) { buttons=value; }
    uint8_t selected_mux() const { return mux; }
    std::vector<uint8_t> take_sound_commands() {
        auto result=std::move(sound_commands);
        sound_commands.clear();
        return result;
    }
    XBoardTimeline board;
private:
    XBoardCpuVisibility cpu;
    std::optional<uint64_t> transaction_ns;
    std::array<uint8_t,8> inputs{};
    uint8_t mux=0,buttons=0xff;
    std::vector<uint8_t> sound_commands;
};
