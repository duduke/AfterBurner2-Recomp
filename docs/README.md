# Documentation guide

| Task | Start here |
| --- | --- |
| Build, play or configure the game | [Project README](../README.md) |
| Scan priorities, current coverage and working rules | [Summarized roadmap](ROADMAP.md#summarized-roadmap) |
| Plan whole-credit fidelity and lifecycle acceptance | [Fidelity plan](FIDELITY_PLAN.md) |
| See implemented enhancements and future proposals | [Enhancement backlog](ENHANCEMENT_PLAN.md) |
| Run daily main checks, engine oracles or recover an instruction | [Workflow](WORKFLOW.md) |
| Build and sideload Android / Android TV | [Android preview](ANDROID.md) |
| Package the macOS preview | [macOS release workflow](WORKFLOW.md#macos-preview-release) |
| Extend presentation, controls or audio | [Enhancement architecture](ENHANCEMENT_ARCHITECTURE.md) |
| Select a starting stage or retry Practice | [Stage Select / Practice](PRACTICE_MODE.md) |
| Shuffle stages while keeping stage 23 last | [Shuffle mode](SHUFFLE_MODE.md) |
| Configure sharp or custom HUD text | [HUD presentation](HUD_PRESENTATION.md) |
| Configure CRT effects or external RetroArch presets | [CRT display](CRT_DISPLAY.md) |
| Try or author the HD player-aircraft pack | [Player-aircraft prototype](SPRITE_REPLACEMENT.md) |
| Extract local ROM sprite references | [Offline sprite extraction](SPRITE_EXTRACTION.md) |
| Configure or understand replacement music | [Music replacement](MUSIC_REPLACEMENT_PLAN.md) — custom audio routing, settings and [Saturn tracklist](MUSIC_REPLACEMENT_PLAN.md#sega-saturn-sega-ages-soundtrack-tracklist) |
| Configure headphone HRTF, speakers or interpolation | [OpenAL Soft cockpit audio](SPATIAL_AUDIO_PLAN.md) |
| Implement rumble or force feedback | [Haptics notes](HAPTICS_PLAN.md) — rumble implemented, advanced FFB planned |
| Extend the visible scene without stretching | [True widescreen plan](WIDESCREEN_PLAN.md) — experimental 16:9/16:10 previews and source 21:9 |
| Find ROM addresses, MAME evidence or validation logs | [Engine notes](ENGINE_NOTES.md) |
| Understand X-Board hardware | [Hardware](HARDWARE.md) |
| Inspect retained reconstruction tests | [Historical Stage 1 baseline](STAGE1_FIDELITY.md) |
| Check dependencies and licenses | [Third-party notices](THIRD-PARTY-NOTICES.md) |

Current status belongs in the roadmap; dated evidence remains in Engine Notes
and Git history. Before code changes, run `python3 tools/workflow.py context`
and read the relevant source and notes.
