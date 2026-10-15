# Documentation guide

| Task | Start here |
| --- | --- |
| Build, play or configure the game | [Project README](../README.md) |
| Scan priorities, current coverage and working rules | [Summarized roadmap](ROADMAP.md#summarized-roadmap) |
| Plan whole-credit fidelity and lifecycle acceptance | [Fidelity plan](FIDELITY_PLAN.md) |
| See implemented enhancements and future proposals | [Enhancement backlog](ENHANCEMENT_PLAN.md) |
| Run tests, captures or recover a missing instruction | [Workflow](WORKFLOW.md) |
| Package the macOS preview | [macOS release workflow](WORKFLOW.md#macos-preview-release) |
| Extend presentation, controls or audio | [Enhancement architecture](ENHANCEMENT_ARCHITECTURE.md) |
| Configure or understand replacement music | [Music replacement](MUSIC_REPLACEMENT_PLAN.md) |
| Configure headphone HRTF, speakers or interpolation | [OpenAL Soft cockpit audio](SPATIAL_AUDIO_PLAN.md) |
| Implement rumble or force feedback | [Haptics notes](HAPTICS_PLAN.md) — rumble implemented, advanced FFB planned |
| Extend the visible scene without stretching | [True widescreen plan](WIDESCREEN_PLAN.md) — experimental 16:9 preview |
| Find ROM addresses, MAME evidence or validation logs | [Engine notes](ENGINE_NOTES.md) |
| Read earlier roadmap checkpoints | [Checkpoint history](CHECKPOINT_HISTORY.md) |
| Understand X-Board hardware | [Hardware](HARDWARE.md) |
| Inspect retained reconstruction tests | [Historical Stage 1 baseline](STAGE1_FIDELITY.md) |
| Check dependencies and licenses | [Third-party notices](THIRD-PARTY-NOTICES.md) |

Current status belongs in the roadmap; historical results retain the scope of
that revision. Before code changes, run `python3 tools/workflow.py context`, then
search the relevant source and engine-note section. Read the rules before
porting, and keep ROM data/captures out of Git.
