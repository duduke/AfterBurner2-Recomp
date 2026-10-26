#!/usr/bin/env python3
"""ROM-free fixtures: ZIP metadata, decompression and destination validation."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
count = 0
with tempfile.TemporaryDirectory(prefix='ab2-rom-zip-') as temp:
    root = pathlib.Path(temp)
    payload = bytes(range(256))
    def check(source, success=True, strides=(1, 2, 4), cwd=None):
        global count
        for stride in strides:
            result = subprocess.run([binary, str(source), 'pass' if success else 'fail', str(stride)],
                                    cwd=cwd, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{source}: {result.stdout}{result.stderr}')
            count += 1
    def archive(name, entries, method=zipfile.ZIP_DEFLATED):
        path = root / name
        with zipfile.ZipFile(path, 'w', compression=method) as out:
            for entry, data in entries:
                out.writestr(entry, data)
        return path
    loose = root / 'loose'
    loose.mkdir()
    (loose / 'chip.bin').write_bytes(payload)
    check(loose)
    for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        check(archive(f'flat-{method}.zip', [('chip.bin', payload)], method))
        check(archive(f'nested-{method}.zip', [('aburner2/chip.bin', payload)], method))
    check(archive('日本語 ROMs.zip', [('chip.bin', payload)]))
    check(archive('missing.zip', [('other.bin', payload)]), False)
    check(archive('size.zip', [('chip.bin', payload[:-1])]), False)
    check(archive('crc.zip', [('chip.bin', bytes(256))]), False)
    check(archive('duplicate.zip', [('chip.bin', payload), ('folder/chip.bin', payload)]), False)
    check(archive('unsupported.zip', [('chip.bin', payload)], zipfile.ZIP_BZIP2), False)
    corrupt = archive('corrupt.zip', [('chip.bin', payload)], zipfile.ZIP_STORED)
    data = bytearray(corrupt.read_bytes())
    data[38] ^= 1  # first byte of stored member, independently invalidates ZIP CRC
    corrupt.write_bytes(data)
    check(corrupt, False)
    truncated = root / 'truncated.zip'
    truncated.write_bytes(data[:40])
    check(truncated, False)
    check(root / 'absent.zip', False)
    auto = root / 'roms'
    auto.mkdir()
    (auto / 'aburner2.zip').write_bytes((root / 'flat-8.zip').read_bytes())
    check('', cwd=root)
    (auto / 'chip.bin').write_bytes(payload)
    check('', cwd=root)
print(f'ROM loader: {count}/{count} synthetic directory/ZIP/negative checks passed')
