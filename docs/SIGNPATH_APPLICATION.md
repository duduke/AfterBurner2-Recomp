# SignPath eligibility inquiry — prepared draft

**Status: not submitted, not approved.** The current project does not satisfy the
published OSI-license requirement. This draft discloses that issue; it must not
be submitted as a declaration that all eligibility conditions are met. No
certificate, signing subscription or sponsorship is claimed.

## Project details

- Project: AfterBurner2-Recomp, a C++ reimplementation of the After Burner II
  arcade host with translated 68000 routines and a shared SDL frontend.
- Project owner and proposed maintainer/reviewer/signing approver:
  [@duduke](https://github.com/duduke), subject to the owner's confirmation.
- Public download/index repository:
  <https://github.com/duduke/AfterBurner2-Recomp-Releases>.
- Development repository (currently private):
  <https://github.com/duduke/AfterBurner2-Recomp>.
- Contact email: to be supplied by the owner in the provider's application.
- Distribution: portable Windows x64 ZIP; matching project/dependency sources
  and notices accompany releases. No ROM images, game artwork or audio from
  the arcade ROMs are distributed. Users supply their own ROMs locally.

## Eligibility questions for SignPath

The project and retained Cannonball/MAME-legacy/Snes9x components include
noncommercial restrictions; see [the project license](../LICENSE.txt),
[license inventory](../LICENSES/README.md) and
[component notices](THIRD-PARTY-NOTICES.md). These are not all OSI-approved
licenses. We recognize that this does not meet your current published terms.
We are not proposing to relabel these components or change third-party licenses.
Is there any supported no-cost route for this project under its actual terms?
If not, we will not claim eligibility for the Foundation program.

The intended runtime also bundles upstream SDL3 3.4.18, OpenAL Soft 1.25.2,
librashader 0.12.0 and LLVM-MinGW runtime DLLs. Five of those DLLs are unsigned.
Their maintainers are separate teams. We understand that a Foundation
subscription cannot simply sign upstream binaries with our project's
certificate. Signing only our EXE would leave the demonstrated DLL blocks
unresolved. What supported upstream-signing or packaging arrangement, if any,
would make this distribution suitable for the program?

## Proposed policy, conditional on acceptance

Only project release binaries built from the specified source revision through
an approved, GitHub-hosted workflow would be submitted. Local diagnostic builds,
user-provided ROMs/assets and unrelated software would never be signed.
Each release would require approval by the owner's designated signing approver.
Upstream DLLs would retain their upstream identities/signatures; no exception
to the provider's rules is assumed.

The game frontend uses local ROMs, settings and optional media/assets. The
frontend source review found no runtime upload, telemetry or network-fetch API
calls. Build/dependency-fetch scripts access upstream distribution sites;
these are separate from gameplay. The owner should confirm the final privacy
statement, including the chosen upstream runtime distribution, before publishing
it as a signing policy.

The portable runtime is uninstalled by deleting its extracted directory; local
preferences can also be removed from SDL's per-user
`AfterBurner2-Recomp/AfterBurner2` preferences directory. An explicitly supplied
`--settings-file` uses the operator's chosen location instead. No certificate
trust changes or security-policy changes are part of installation.

## Owner/provider steps before this becomes an application

1. Resolve the stated license eligibility and upstream-signing questions. Do
   not mark the OSI-license condition as satisfied under the present licenses.
2. Confirm contact details, signing roles and MFA for each account. MFA status
   has not been verified by the preparation scripts.
3. If SignPath confirms a supported route, create/access the approved account,
   install its GitHub integration with only the required repository access,
   and configure its project, artifact rules and manual-approval policy. This
   grants a provider access to private source; the owner must perform or
   explicitly authorize that account connection.
4. Add the required public **Code signing policy** link and any provider credit
   only when accurate. Do not announce free signing as already provided.
5. Build on GitHub-hosted runners. The current locally built performance EXE is
   a validation reference, not a GitHub-origin artifact eligible for submission.
   Pin product/version metadata and allow only the actual project release files.
6. Resolve signed upstream dependencies, obtain the signatures, run the
   [signature/package/launch checks](WINDOWS_SIGNING.md), then publish.

Prepared from [Foundation eligibility terms](https://signpath.org/terms) and
[GitHub build-origin requirements](https://docs.signpath.io/trusted-build-systems/github).
No request has been sent and no provider account or repository integration has
been created.
