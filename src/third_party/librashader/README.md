# librashader C API

`librashader.h` is the unmodified MIT-licensed public C header shipped with
[librashader v0.12.0](https://github.com/SnowflakePowered/librashader/releases/tag/librashader-v0.12.0).
Its MIT copyright and permission notice are retained in the header.

The optional runtime is loaded dynamically; no runtime binary or shader pack is
vendored. `tools/deps/fetch_librashader.py` records the pinned release, archive
checksum, source URL and license URLs in the local dependency manifest. The
runtime may be distributed under MPL 2.0; its full license is retained in
`LICENSE.MPL-2.0`. Individual user shader packs retain their own licenses.

See [NOTICE.md](NOTICE.md) for the pinned source archive and redistribution notes.
