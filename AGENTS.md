# Instructions for AI assistants

This project reimplements SEGA's After Burner II arcade game by porting its
68000 code to C++, with the goal of gameplay identical to the arcade ROM.

Before changing anything, read "Working on the remaining tasks" at the top
of [docs/ROADMAP.md](docs/ROADMAP.md). It has the rules (never invent
behaviour, translate instruction by instruction, test every port against
MAME, never commit ROM data or captures), the commands, and the next task.

For a compact starting read, run `python3 tools/workflow.py context`. This
prints those rules, the current checkpoint and ordered acceptance tasks
directly from the authoritative docs. Then read only the relevant sections
of ENGINE_NOTES and source with `rg`; avoid dumping historical docs or large
generated instruction tables into chat. See [docs/WORKFLOW.md](docs/WORKFLOW.md).

Use `python3 tools/workflow.py run <profile-or-make-target>` for bounded
output and full local logs. Start with focused targets, reuse existing
captures, and use ROADMAP's validation cadence: main-functionality and affected
checks for daily work and non-engine releases; full oracle/mutation checks only
for engine changes. Documentation-only changes need diff and policy/command/link
inspection. Retain all fixtures and mutations. Do not treat a
focused pass as whole-host acceptance or remove fixture coverage to save time.
