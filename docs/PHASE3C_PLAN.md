# Phase 3c implementation plan

Phase 3b provides shared arcade RAM, math chips, road/scenery ports and video
endpoints. The next milestone replaces sprite scheduling and the native
`OSprites` sorter/blitter. It does not establish whole-game fidelity: player,
enemy and weapon behavior remain Phase 3d, and stage dispatch remains 3e.

1. **Capture the frame boundary first.** Map the complete `$014674` handler
   and every callee, capturing both 30 Hz logic and intervening display frames.
   Record the four pointer pairs `$2A3C04..$2A3C1C`, frame mutex `$29C0D6`,
   lamps, road/sprite latch order and I/O transactions. Separate other-CPU
   writes and use SP/USP according to SR. Identify hooks that depend on 3d/3e
   before implementing the handler; expose explicit subsystem boundaries
   rather than substitute guessed behavior for those calls.
2. **Port sprite emission and hardware blits.** Translate `append_sprite`
   (`$00B628`) and `$00B7C2`/`$00B7D2`, retaining arcade object/request layout,
   bounds, sentinels and terminators. Capture empty, populated and boundary
   lists and verify the alternate `+$C00` interpolated list. Add bus sprite
   endpoints so Sprite RAM writes and draw triggers preserve buffer order.
3. **Connect the verified sub pass.** Run `OSubSprites` between main request
   publication and main hardware blit, with the real pointer swaps and mutex.
   Retire the native depth sort/hardware descriptor writer. Any adapter for
   still-native object producers must state its limits; verify the arcade
   descriptor encoding separately and leave AI/flight behavior for 3d.
   Exercise the existing skip-sort, bucket-spill and clipping gaps with
   directed captures, clearly separate from full gameplay runs.
4. **Port palette scheduling.** Translate `$014A86` fade/flash and `$013F92`
   animation, following runtime ROM pointers and the original cadence.
   Capture start, continuation, terminal counts and idle paths; remove native
   palette timing only after the ports pass their write-set/I/O comparisons.
5. **Wire the 30/60 Hz loop.** Call object logic only on the ROM's logic phase;
   draw the selected normal/interpolated sprite list on each display frame.
   Keep road generation once per display frame and validate its latch order
   against the captured handler. Update native adapters/tests explicitly for
   the cadence change; do not silently rewrite constants to compensate.

Each port must pass its MAME oracle before wiring, fail a deliberate constant
mutation, and document exercised paths and input-race skips. Run `make test`,
`make verify-stage1` and `make oracle-test` before committing. Add paired
logic/interpolation frame captures that compare sprite RAM, draw triggers and
rendered output for the implemented pipeline. Whole-credit RAM/video equality
remains the acceptance criterion for 3f, not a claim made by 3c.

Finish with updated `ROADMAP.md` and `ENGINE_NOTES.md`, a separate PR containing
no ROM data/captures, and an explicit list of remaining 3d/3e boundaries.
