#!/usr/bin/env python3
"""Reject routing sequence feedback through the old limited presentation tap.

The Make target first runs verify-feedback-sequences-native as the baseline.
Only GameSession's presentation routing is overlaid; original engine objects,
fixtures, options and compiler commands are retained.
"""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    plan = subprocess.check_output(['make', '--no-print-directory', '-Bn', 'build/feedback_sequence_integration'], cwd=ROOT, text=True)
    commands = [shlex.split(line) for line in plan.splitlines() if line.startswith(('c++ ', 'clang++ ', 'g++ ', '/'))]
    compile_command = next(c for c in commands if '-c' in c and 'tests/feedback_sequence_integration.cpp' in c)
    link_command = next(c for c in reversed(commands) if '-o' in c and c[c.index('-o')+1]=='build/feedback_sequence_integration')
    source=(ROOT/'src/main/engine/game_session.hpp').read_text()
    before='presentation_phase=!(capture_feedback_||capture_object_audio_);'
    assert source.count(before)==1, 'observer mutation anchor must be unique'
    log_path=ROOT/'build/haptics/observer-mutation.log'
    log_path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ab2-feedback-observer-') as tmp:
        overlay=Path(tmp);header=overlay/'engine/game_session.hpp';header.parent.mkdir()
        header.write_text(source.replace(before,'presentation_phase=true;'))
        obj=str(overlay/'check.o');exe=str(overlay/'check')
        compile_command.insert(1,'-I'+str(overlay))
        compile_command[compile_command.index('-o')+1]=obj
        link_command[link_command.index('build/native_obj/tests/feedback_sequence_integration.o')]=obj
        link_command[link_command.index('-o')+1]=exe
        with log_path.open('w') as log:
            subprocess.run(compile_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run(link_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            result=subprocess.run([exe],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        output=log_path.read_text()
        if result.returncode==0 or 'native sequence event count differs from MAME frame' not in output:
            raise RuntimeError('observer mutation survived or failed for an unrelated reason; see '+str(log_path))
    print('PASS rejected filtered-sequence-observer; baseline is verify-feedback-sequences-native')

if __name__=='__main__':
    main()
