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

The [SignPath Foundation terms](https://signpath.org/terms) require OSI-approved
licenses. The project's [license](../LICENSE.txt) prohibits commercial use, and
retained dependencies also have noncommercial terms. Consequently the project
does not meet the published free-program requirements in its current form.
At the owner's request, a [candid eligibility-inquiry draft](SIGNPATH_APPLICATION.md)
was prepared and emailed to SignPath on 4 November 2026 with the owner's
explicit authorization. No formal application, approval or change of license is claimed. Foundation restrictions on
signing upstream DLLs would also need resolving before choosing that route.

Use an existing publicly trusted CA-issued certificate/HSM or an approved
cloud signing account. [Microsoft's signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
describe identity validation and geographic eligibility; provider enrollment
remains an external prerequisite. Do not put private keys or passwords in chat,
source files, artifacts or GitHub workflow text.

## Current candidate and audit

`build/windows-signing-release/unsigned-runtime/` is an isolated signing input:
11 PE files, `Launch.cmd` and `LICENSES.txt`, with no ROMs, tests or settings.
It reuses the checksum-verified original runtime's dependencies and notices,
replacing only the EXE with the validated performance build:
`c56180f74d4bd0026a1577778a5dc661d3f5d803242c8351e79a688ea9e79524`.
This is **unsigned staging**, not a completed release package or source companion.

The Windows audit finds these six unsigned files:

- `afterburner2.exe`
- `SDL3.dll`
- `OpenAL32.dll`
- `librashader.dll`
- `libc++.dll`
- `libunwind.dll`

The three Microsoft C++ runtime DLLs and two Microsoft DirectX DLLs already have
valid timestamped signatures. Preserve those signatures. The five-file vendor
control passes verification; unsigned, locally trusted ad-hoc and tampered
controls fail. The complete unsigned runtime correctly fails. All checks run
with Smart App Control On, without changing trust or security policies.
Evidence and negative-control files remain under `build/windows-signing-release/`
and `C:\Games\AB2-signing-release`.

## Sign and verify

For a CA-issued key already installed in a Windows certificate provider/HSM,
with the Windows SDK Signing Tools installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\sign_release.ps1 `
  -RuntimeDirectory C:\path\to\staged-runtime `
  -CertificateThumbprint PROVIDER_CERTIFICATE_THUMBPRINT `
  -ReceiptPath C:\path\to\new-signature-receipt.json
```

The script rejects a self-signed signing identity, preserves already valid vendor
signatures, signs remaining files using SHA-256 and an RFC 3161 timestamp, then
verifies the complete runtime. It uses the provider's existing key without
exporting it. No successful production signing run is claimed until a provider
is available. For a cloud signer that does not expose a Windows certificate,
sign through that provider first, then run verification separately:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\verify_release_signatures.ps1 `
  -RuntimeDirectory C:\path\to\signed-runtime `
  -ReceiptPath C:\path\to\new-signature-receipt.json
```

Verification requires valid Authenticode, timestamps, approved public roots and
an exact flat runtime inventory. It rejects ad-hoc signatures even if locally
trusted. Windows AuthRoot entries and Microsoft's published built-in 2010/2011
root fingerprints are recognized. If necessary, `-ApprovedRootThumbprint` pins a
public CA root independently confirmed with the provider; never use it to admit
a development root. Root fingerprints are listed in
[Microsoft's PKI repository](https://www.microsoft.com/pkiops/docs/repository.htm).
`-FilesOnly` is solely for scoped controls and never produces a full-runtime
receipt. Existing receipts are not overwritten.

## Package and publish

The compact packager now requires an explicit signing mode. For preparatory
artifacts, add `--unsigned-signing-input`; their names include
`UNSIGNED-signing-input`. For the actual release, from a clean committed tree
with the existing pinned build dependencies/notices restored:

```sh
python3 tools/package_windows_runtime.py "$AB2_VERSION" --label "$AB2_LABEL" \
  --build build/win --msvc-redist "$AB2_MSVC_REDIST" \
  --verified-runtime "$AB2_SIGNED_RUNTIME" \
  --signature-receipt "$AB2_SIGNATURE_RECEIPT"
```

The packager checks a passing full-runtime receipt and each signed file's digest
before copying it. It also compares PE payloads with the original build,
allowing only Authenticode metadata/checksum/padding changes. It records signing evidence and final signed hashes in the
matching runtime/source manifests. A receipt is local pipeline evidence, not a
cryptographic attestation; obtain it by running the checker on the actual signed
files. A changed or incomplete payload must be verified again.

Extract the resulting ZIP to a fresh directory. With Smart App Control On,
launch normal SDL audio, HRTF/OpenAL and Slang presets so every optional DLL path
is exercised; inspect new Code Integrity blocks. Keep the game window in front,
using its SDL window title/handle rather than its console PID. Retain logs,
package/source identities, notices and checksums. Only then upload the runtime,
matching source companion and checksums and restore public download links.
Existing whole-game performance/recording limits remain in the release notes.
