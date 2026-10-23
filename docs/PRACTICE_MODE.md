# Stage Select / Practice

The local native application offers **Practice** in the initial and pause menus.
Choose a stage with Left/Right, then select **Start Practice**. The menu supports
keyboard, controller and mouse navigation. Stage numbers are the ordered ROM
slots, 1..23 for this ROM set; the count and descriptors are read at runtime.

Starting Practice replaces the current run with a fresh board using the selected
DIP settings. It supplies the established coin/start pulses, then uses the
existing directed ROM stage transition, including alternate road palette
preparation. Native lives, damage, score, weapons and subsequent stage progression
continue normally. It retains fresh first-stage accumulated state rather than
reconstructing the complete history of an arcade run.

**F5** retries the stage that started the active Practice session. Browsing a
different stage and cancelling leaves that retry target unchanged. Escape/F1
opens the pause menu; Practice → **Return to Arcade** creates a fresh cold board
and restores normal coin/start play. The gameplay overlay and window title
identify the Practice retry stage. This choice is session state, not a saved
preference. It does not change the native frame pixels or timers.

For a direct launch, use:

```bash
./build/afterburner2-release --practice-stage 8
```

`--practice-stage` and the existing debug `--stage` are mutually exclusive.
Practice is unavailable while `--record` is active, and either selected-stage
CLI option is rejected with `--record`. Physical recordings remain natural
cold-boot input runs. The older debug launch and sweep commands remain available.
The separate [Shuffle mode](SHUFFLE_MODE.md) arranges a continuous random stage
order; it is available alongside Practice and has its own retry state.

## Implementation and evidence

`StageCatalog` exposes a read-only stage ordinal, selector, descriptor and event
program. `GameSession::enter_practice_stage` restricts the execution interface to
the existing directed entry. `frontend/native_practice.hpp` owns cold restart,
DIP restoration, physical boot and the ephemeral retry target. The application
resets music, feedback, presentation history, audio queues and pacing around
each restart. Hidden boot output updates replacement-music state without
queueing playback. Menus never write gameplay state.

The catalog oracle passively observes the executing MAME ROM's copied selector
list and reads its descriptors and event programs. It verifies metadata; it
does not prove complete selected-stage state equivalence. The existing independent
palette oracle covers the fourteen alternate-gradient words for entries 5, 8,
13 and 17. Practice lifecycle comparisons use the existing debug startup as a
reference and are explicitly separate from MAME evidence.

Focused checks:

```bash
python3 tools/workflow.py run capture-stage-catalog
python3 tools/workflow.py run verify-stage-catalog check-stage-catalog-mutations verify-native-stage-palette
python3 tools/workflow.py run test verify-stage1 verify-native-menu verify-native-practice
```

`verify-practice-mode` is included in `verify-stage1`; catalog comparisons and
four retained negative controls are included in `oracle-test`. Captures and
menu/frame images stay under ignored `build/oracle/`. Full selected-stage
CPU/device/RGB/audio equivalence, broader physical-controller/platform acceptance,
stage looping and Time Attack remain open. This feature does not close natural
progression or whole-credit fidelity acceptance.

The local `test`, complete `verify-stage1`, menu and CLI launch checks pass.
See the [validation record](ENGINE_NOTES.md#stage-select--practice-first-increment-2026-10-23)
for independent MAME counts, rejected controls, logs and exact scope.
