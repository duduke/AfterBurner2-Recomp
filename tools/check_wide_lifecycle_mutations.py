#!/usr/bin/env python3
"""After positive lifecycle replays, prove the MAME observation gate rejects a changed mode."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
result=subprocess.run(['./build/wide_lifecycle_visibility','neutral','--bad-reference'],cwd=root,capture_output=True,text=True)
output=result.stdout+result.stderr
log=root/'build/oracle/widescreen/lifecycle-reference-mutation.log';log.parent.mkdir(parents=True,exist_ok=True);log.write_text(output)
assert result.returncode==1 and 'Wide lifecycle observation differs from MAME at frame 0 column 2' in output,output[-2000:]
print('Wide lifecycle altered MAME mode: rejected')
