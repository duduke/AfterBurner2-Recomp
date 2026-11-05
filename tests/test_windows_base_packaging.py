#!/usr/bin/env python3
"""Negative controls for standalone import and signed-payload verification."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import package_windows_runtime as runtime


class WindowsBase(unittest.TestCase):
    def test_import_allowlist(self):
        for dependency in ('SDL3.dll','OpenAL32.dll','librashader.dll','libc++.dll',
                           'libunwind.dll','VCRUNTIME140.dll','D3DX9_43.dll',
                           'D3DCompiler_43.dll','surprise.dll'):
            report='file format coff-x86-64\n DLL Name: '+dependency
            with self.subTest(dependency=dependency), patch.object(runtime,'command',return_value=report):
                with self.assertRaisesRegex(SystemExit,'Unbundled dependency'):
                    runtime.audit_imports(Path('.'),['afterburner2.exe'],Path('bin/clang++'),True)
        report='file format coff-x86-64\n DLL Name: KERNEL32.dll\n DLL Name: api-ms-win-crt-runtime-l1-1-0.dll'
        with patch.object(runtime,'command',return_value=report):
            self.assertEqual(len(runtime.audit_imports(Path('.'),['afterburner2.exe'],Path('bin/clang++'),True)['afterburner2.exe']),2)
        with patch.object(runtime,'command',return_value='file format coff-i386'):
            with self.assertRaisesRegex(SystemExit,'Not an x64 PE'):
                runtime.audit_imports(Path('.'),['afterburner2.exe'],Path('bin/clang++'),True)

    def test_signed_payload_inventory_hash_and_code(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);original=root/'original.exe';signed=root/'afterburner2.exe'
            # Synthetic PE32+ header. No executable/third-party/ROM data.
            image=bytearray(512);image[:2]=b'MZ';struct.pack_into('<I',image,60,64)
            image[64:68]=b'PE\0\0';struct.pack_into('<H',image,88,0x20b)
            original.write_bytes(image)
            struct.pack_into('<I',image,88+64,123)  # signing checksum
            struct.pack_into('<II',image,88+112+32,512,8)
            signed.write_bytes(image+b'certificate'[:8])
            item={'File':'afterburner2.exe','Passed':True,'Status':'Valid','Timestamped':True,
                  'SHA256':runtime.digest(signed)}
            receipt={'Schema':1,'Passed':True,'FullRuntime':True,'Files':[item]}
            path=root/'receipt.json'
            def verify():
                path.write_text(json.dumps(receipt))
                return runtime.verified_signatures(root,path,{'afterburner2.exe':original})
            self.assertEqual(verify(),receipt)
            receipt['Files']=[]
            with self.assertRaisesRegex(SystemExit,'exact runtime PE inventory'):verify()
            receipt['Files']=[item];receipt['FullRuntime']=False
            with self.assertRaisesRegex(SystemExit,'full-runtime'):verify()
            receipt['FullRuntime']=True;item['Timestamped']=False
            with self.assertRaisesRegex(SystemExit,'mismatch'):verify()
            item['Timestamped']=True;changed=bytearray(signed.read_bytes());changed[400]=1;signed.write_bytes(changed)
            with self.assertRaisesRegex(SystemExit,'mismatch'):verify()
            item['SHA256']=runtime.digest(signed)
            with self.assertRaisesRegex(SystemExit,'changed the built PE payload'):verify()


if __name__=='__main__':unittest.main()
