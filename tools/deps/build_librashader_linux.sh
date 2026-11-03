#!/bin/sh
# Optional Linux Vulkan runtime; run inside the same SDK as the application.
set -eu
cd "$(dirname "$0")/../.."
test "$(uname -s)" = Linux || { echo 'Run this helper on Linux.' >&2; exit 1; }
# Pin the compiler too; install this toolchain separately with rustup (minimal).
test "$(rustc --version | cut -d ' ' -f 2)" = 1.99.1 || {
  echo 'Rust 1.99.1 is required; select it with rustup before building.' >&2; exit 1;
}
deps="$PWD/build/deps"
archive="$deps/librashader-v0.12.0-source.tar.gz"
mkdir -p "$deps"
if [ ! -f "$archive" ]; then
  curl --proto '=https' --tlsv1.2 -fL \
    https://github.com/SnowflakePowered/librashader/archive/refs/tags/librashader-v0.12.0.tar.gz \
    -o "$archive"
fi
printf '%s  %s\n' 4bf8cf2489d00848dcabbf2163204093776082da4217d5a5db45e4cbf335cedf "$archive" | sha256sum -c -
source="$deps/librashader-librashader-v0.12.0"
test -d "$source" || tar -xf "$archive" -C "$deps"
(cd "$source" && cargo build --locked --release --jobs "${AB2_JOBS:-2}" \
  -p librashader-capi --no-default-features --features runtime-vulkan)
mkdir -p "$deps/librashader"
cp "$source/target/release/liblibrashader_capi.so" "$deps/librashader/"
python3 - <<'PY'
import hashlib, json, platform, subprocess
from pathlib import Path
root = Path('build/deps')
runtime = root / 'librashader/liblibrashader_capi.so'
manifest = dict(version='0.12.0', abi=2, api=5, architecture=platform.machine(),
                source_url='https://github.com/SnowflakePowered/librashader/archive/refs/tags/librashader-v0.12.0.tar.gz',
                source_sha256=hashlib.sha256((root/'librashader-v0.12.0-source.tar.gz').read_bytes()).hexdigest(),
                runtime_sha256=hashlib.sha256(runtime.read_bytes()).hexdigest(),
                rustc=subprocess.check_output(['rustc','--version'],text=True).strip(),
                cargo=subprocess.check_output(['cargo','--version'],text=True).strip(),
                features=['runtime-vulkan'], locked=True)
(root/'librashader/build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(runtime)
PY
