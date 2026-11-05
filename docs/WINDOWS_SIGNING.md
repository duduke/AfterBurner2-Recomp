# Windows release signing

Public Windows downloads remain withdrawn until the executable **and all bundled
DLLs** have trusted signatures and an extracted package launches with Smart App
Control On. A checksum, GitHub attestation or a locally trusted self-signed
certificate does not meet this requirement. No full engine regression/oracle run
is needed for this frontend/packaging change; reuse the unchanged main and
frontend evidence recorded in [Windows performance](WINDOWS_PERFORMANCE.md).

## Signing identity required

No release-signing credentials, variables or environments are configured in the
development GitHub repository, and no code-signing private key is available to
the Windows `codex` account. The existing ad-hoc certificate is not a public
release identity. A signing provider must perform its identity/issuance process;
this cannot be replaced by generating another local certificate.

SignPath declined the original inquiry on 5 November 2026 because of the
noncommercial licensing, and confirmed that upstream DLLs must be signed by
their own maintainers. The source repository is now public, original code is
GPL-3.0-or-later, and the noncommercial filters and unused legacy audio classes
have been removed from the current tree. They remain on the explicitly
historical branch under their original terms. This addresses the identified
license/public-source prerequisites; **SignPath has not approved the revised
project**. See [application status](SIGNPATH_APPLICATION.md).

Use an existing publicly trusted CA-issued certificate/HSM or an approved
cloud signing account. [Microsoft's signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
describe identity validation and geographic eligibility; provider enrollment
remains an external prerequisite. Do not put private keys or passwords in chat,
source files, artifacts or GitHub workflow text.

## Standalone base and optional runtimes

The current compact packager defaults to `--profile base`. Its only PE is
`afterburner2.exe`; SDL and compiler runtimes must be linked statically.
The exact runtime inventory is the EXE, `Launch.cmd` and `LICENSES.txt`.
OpenAL Soft is loaded only when spatial audio is requested. A missing, blocked
or incompatible runtime reports a reason and falls back to SDL stereo.
`AB2_OPENAL_RUNTIME` selects an explicit compatible runtime path; otherwise the
loader checks beside the executable. The Slang host already loads librashader
optionally and reports an unavailable runtime without preventing base gameplay.
Built-in sprite filters and CRT effects do not require librashader.

No OpenAL, librashader, SDL, compiler-runtime or Microsoft redistributable DLL
is bundled in the base. Protected Windows still requires trusted signatures
on external DLLs when those optional features load them. Installing an unsigned
DLL separately does not solve that feature's trust requirement. Obtain a
compatible signed distribution from its upstream maintainer; do not disable
Smart App Control or sign their DLLs with this project's identity.

The earlier eleven-PE diagnostic distribution and failed-signature controls
remain archived locally under `build/windows-signing-release/`. They are not
the new base release. `--profile legacy` retains its exact inventory for
historical checks, not an approved publication route.

## Hosted unsigned build

`.github/workflows/windows-base.yml` is a manual GitHub-hosted build. It pins
checkout/upload actions and the LLVM-MinGW distribution, SDL, OpenAL headers and
Vulkan headers by immutable commit or SHA-256. `tools/windows/build_base.py`
builds static SDL and the app, then the compact packager audits imports and
creates explicitly unsigned input plus corresponding sources. The workflow
has read-only repository permission, no signing keys and no release publishing.
SignPath integration and artifact rules still require provider/owner setup.

## Sign, verify and package

After provider approval, sign the project's EXE through the approved service.
For a CA-issued key already available through a Windows certificate provider,
`tools/windows/sign_release.ps1` signs only `afterburner2.exe`. It rejects
unsigned upstream files before any signing operation. Both scripts default to
`-Profile base`; the verifier requires timestamped Authenticode, an approved
public root and the exact flat inventory. It rejects locally trusted ad-hoc
certificates. Neither script changes certificate trust or security policy.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\verify_release_signatures.ps1 `
  -RuntimeDirectory C:\path\to\signed-runtime `
  -ReceiptPath C:\path\to\new-signature-receipt.json
```

From a clean committed source tree, prepare explicitly unsigned input with
`--unsigned-signing-input`. It must not be published as a signed release.
After signing and Windows verification:

```sh
python3 tools/package_windows_runtime.py "$AB2_VERSION" --label "$AB2_LABEL" \
  --build build/win --profile base \
  --verified-runtime "$AB2_SIGNED_RUNTIME" \
  --signature-receipt "$AB2_SIGNATURE_RECEIPT"
```

The packager checks exact PE inventory, signed file hashes and a passing full
receipt. It compares signed and original PE payloads, allowing only signing
metadata/checksum/padding differences. Imports must be Windows OS libraries;
unsigned external runtime imports are rejected. The matching source companion
includes project and statically linked dependency sources/notices. Receipts
are pipeline evidence, not cryptographic attestations.

Before publication, extract the final signed ZIP and launch with Smart App
Control On. Verify base gameplay with both optional libraries absent, persisted
spatial/shader settings, and clear fallback messages. Optional features require
separate compatible trusted-runtime launch checks; no provider approval or
protected Windows launch is claimed from cross-build/import checks alone.
Publish only after trusted signing and extracted-package validation pass.
