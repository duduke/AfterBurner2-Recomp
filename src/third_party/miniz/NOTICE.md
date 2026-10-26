# miniz 3.1.0

Source: https://github.com/richgel999/miniz/tree/3.1.0
License: MIT (see LICENSE).

Upstream reader, inflate and common sources/headers; only line endings and
trailing whitespace are normalized. The local
miniz_export.h supplies the empty export macro for our static embedded build.
romloader.cpp compiles the reader with writing, deflate and zlib APIs disabled;
this keeps standalone ROM/oracle executables free of new link dependencies.
No files are extracted to disk. Allocation for a chip is limited to its
expected ROM size before decompression.
