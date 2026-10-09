# YMFM OPM core

These five files are unmodified copies of the BSD-3-Clause YMFM OPM core
from the local MAME source at commit
`6b511c0a858b8de0499d38cf382f0ef666ebf2cc` (`3rdparty/ymfm/src`).
Copyright Aaron Giles; license is in `LICENSES/YMFM.txt` and each source header.

`XBoardAudioDeadlines` supplies native stream clocks, timer scheduling, busy
state and synchronized mode callbacks. `XBoardAudioRuntime` now uses it in the
Stage 1 sound backend. Acceptance checks MAME's raw pre-routing PCM/FM streams
and exact mixed/resampled speaker output. These sources contain no arcade ROM
data.
