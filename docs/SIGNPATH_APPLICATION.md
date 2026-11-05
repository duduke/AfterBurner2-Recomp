# SignPath application status

The original eligibility inquiry was emailed on 4 November 2026. SignPath's
5 November reply declined the noncommercial project and confirmed there is no
Foundation exception for its license or permission to sign upstream DLLs.

The owner subsequently authorized these changes:

- Public source: https://github.com/duduke/AfterBurner2-Recomp
- Original code: GPL-3.0-or-later, with the full text in `LICENSE.txt` and
  scope in `COPYRIGHT.md`. No third-party license was changed.
- Remove Snes9x 2xSaI/Super2xSaI and unused Cannonball/MAME-legacy audio classes
  from current source/builds. Original code, fixtures and notices remain on
  `archive/noncommercial-filters-and-audio` under their original terms.
- Standalone base: static SDL/compiler runtimes; no upstream DLLs bundled.
  Spatial audio and Slang are optional external runtimes with graceful fallback.

## Follow-up sent — 5 November 2026

The owner explicitly authorized the revised inquiry, which was emailed as a
reply to SignPath on 5 November. It linked the public source, license scope,
historical branch and successful [hosted unsigned build](https://github.com/duduke/AfterBurner2-Recomp/actions/runs/42081690714).
It also disclosed earlier branches/releases retaining their original terms,
asked about static-library eligibility and the public-release prerequisite, and
made clear that separate installation does not make unsigned external DLLs
trusted. No provider approval or new response is claimed.

The follow-up raised these points:

Thank you for clarifying the Foundation requirements. We have published the
source repository, licensed our original code GPL-3.0-or-later and removed the
noncommercial third-party components from the current source and build.
The removed components remain only in an explicitly historical branch, under
their original terms. Would that historical branch affect eligibility for new
releases whose source and binary inputs exclude those components?

Our revised Windows base statically links SDL (zlib) and LLVM/MinGW runtimes
under their upstream licenses and exceptions. It bundles only our own EXE,
launcher and notices. Optional OpenAL Soft/librashader are externally supplied
and loaded only when requested, with fallback if unavailable or blocked. We
will not submit upstream DLLs for signing with our identity. Does this revised
distribution satisfy the licensing and component-identity requirements?

We understand eligible artifacts must originate in an approved GitHub-hosted
workflow, with required source review and signing approval. Please advise the
next application steps and any source/build policy requirements for this layout.

## Still required

Provider confirmation and account approval; owner-controlled account/MFA and
signing roles; approved GitHub integration and artifact rules; a trusted hosted
build; actual signing; Windows validation with protection On. No signing
credentials or approval exist yet. Do not announce Foundation sponsorship or
restore Windows downloads before those steps succeed.

References: [Foundation terms](https://signpath.org/terms),
[GitHub build origin](https://docs.signpath.io/trusted-build-systems/github),
[signature and package checks](WINDOWS_SIGNING.md).
