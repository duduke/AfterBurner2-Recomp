#!/usr/bin/env python3
"""Retained negative controls for the passive feedback tap and SDL mixer."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ('wrong-cannon-producer', 'engine/feedback_cues.hpp', 'p.pc==0x9ef6', 'p.pc==0x9ef8', 'fixture'),
    ('wrong-missile-producer', 'engine/feedback_cues.hpp', 'p.pc==0x9ebc', 'p.pc==0x9eba', 'fixture'),
    ('wrong-player-impact', 'engine/feedback_cues.hpp', 'p.pc==0x9c20', 'p.pc==0x9c22', 'fixture'),
    ('attract-rumble', 'engine/feedback_cues.hpp', '!flight || p.cpu', 'p.cpu', 'unit'),
    ('early-afterburner', 'engine/feedback_cues.hpp', '>=0xfd', '>=0xfc', 'unit'),
    ('overlap-cancellation', 'frontend/native_haptics.hpp', 'std::max(v[c],strength[i][c]*envelope)', 'strength[i][c]*envelope', 'unit'),
    ('repeated-frame-pulse', 'frontend/native_haptics.hpp', 'f.number<=last_frame', 'f.number<last_frame', 'unit'),
    ('missing-explicit-stop', 'frontend/native_haptics.hpp', 'send({},0);', '/* missing stop */', 'unit'),
    ('trigger-crosswire', 'frontend/native_haptics.hpp', 'SDL_RumbleGamepadTriggers(pad,v[2],v[3],ms)', 'SDL_RumbleGamepadTriggers(pad,v[3],v[2],ms)', 'unit'),
    ('lost-reenable-events', 'frontend/native_haptics.hpp', 'settings.enabled=!settings.enabled;mixer.restart();', 'settings.enabled=!settings.enabled;', 'unit'),
    ('damage-cut-short', 'frontend/native_haptics.hpp', 'damage=f.damage&&s.impact;', 'damage=false;', 'unit'),
    ('missile-wrong-side', 'frontend/native_haptics.hpp', 'if(missile_right)', 'if(!missile_right)', 'unit'),
    ('effect-level-ignored', 'frontend/native_haptics.hpp', 'FeedbackSettings::gain(s.impact)', '1.f', 'unit'),
    ('damage-hidden-intact', 'engine/feedback_cues.hpp', 'state<=4', 'state<=5', 'unit'),
    ('missile-side-inverted', 'engine/feedback_cues.hpp', '!(uint16_t(launches+1)&1)', 'bool(uint16_t(launches+1)&1)', 'details'),
    ('missing-touchdown', 'engine/feedback_sequences.hpp', 'p.pc==0x11406 && a==base_record+60', 'p.pc==0x11408 && a==base_record+60', 'sequence'),
    ('wrong-coupling-pose', 'engine/feedback_sequences.hpp', 'p.pc==0x165a6 && a==0x3f9102', 'p.pc==0x165ac && a==0x3f9102', 'sequence'),
    ('refill-before-audio', 'engine/feedback_sequences.hpp', 'p.pc==0xb81e && (a&~1u)', 'p.pc==0x16cf8 && (a&~1u)', 'sequence'),
    ('airbase-liftoff-early', 'engine/feedback_sequences.hpp', 'base_speed<0x140', 'base_speed<0x100', 'sequence'),
    ('sequence-flat-spool', 'frontend/sequence_haptics.hpp', '.07f+.15f*ramp*triangle(90)', '.07f+.15f*ramp', 'unit'),
    ('missing-coupling-kick', 'frontend/sequence_haptics.hpp', 'else if(i==2)', 'else if(i==20)', 'unit'),
    ('merged-refill-bumps', 'frontend/sequence_haptics.hpp', 'duration{280,230,170,34}', 'duration{280,230,170,120}', 'unit'),
    ('duplicate-sequence-cue', 'frontend/sequence_haptics.hpp', 'e.sequence<=last_sequence', 'false', 'unit'),
    ('attract-sequence-motion', 'engine/feedback_sequences.hpp', 'if(mode==15){', 'if(mode==1){', 'unit'),
    ('sequence-level-ignored', 'frontend/sequence_haptics.hpp', 'pulse[c]*Settings::gain(level)', 'pulse[c]', 'unit'),
    ('refill-ahead-of-audio', 'frontend/sequence_haptics.hpp', 'now+refill_delay_ms+(refill_delay_ms?offset:0)', 'now', 'unit'),
    ('missing-watchdog', 'frontend/native_haptics.hpp', 'now-fresh>=100', 'false', 'unit'),
]

def main():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True).split()
    logs = ROOT / 'build/haptics/mutations'
    logs.mkdir(parents=True, exist_ok=True)
    for name, header, before, after, suite in MUTATIONS:
        source = (ROOT / 'src/main' / header).read_text()
        if source.count(before) != 1:
            raise RuntimeError(f'{name}: mutation anchor is not unique')
        with tempfile.TemporaryDirectory(prefix='ab2-feedback-') as tmp:
            overlay = Path(tmp)
            target = overlay / header
            target.parent.mkdir(parents=True)
            mutated = source.replace(before, after)
            if name == 'merged-refill-bumps':
                assert mutated.count('1.f-age/34.f') == 1
                mutated = mutated.replace('1.f-age/34.f', '1.f-age/120.f')
            target.write_text(mutated)
            test = 'tests/feedback_sequence_fixture.cpp' if suite == 'sequence' else 'tests/feedback_fixture.cpp' if suite in ('fixture', 'details') else 'tests/native_haptics_regression.cpp'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(overlay), '-Isrc/main', test]
            if suite in ('fixture', 'details'):
                command += ['src/main/engine/mainbus.cpp', 'src/main/engine/subbus.cpp']
            executable = overlay / 'check'
            command += ['-o', str(executable), *flags]
            with (logs / (name+'.log')).open('w') as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                run = [str(executable)] + (['build/oracle/feedback_details.bin' if suite == 'details' else 'build/oracle/feedback.bin'] if suite in ('fixture', 'details') else [])
                result = subprocess.run(run, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode == 0:
                raise RuntimeError(f'{name}: mutation survived')
            print(f'PASS rejected {name}', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
