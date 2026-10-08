#!/usr/bin/env node
// Admission-only pilot: run unmodified REA source without configuring an
// agent or installing a provider. ROMs/results stay under ignored build/.
import { registerHooks } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const commit='84a17d55199b1b46493aea6bf4e41029a3b200fc';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
if(process.argv.length!==3)throw new Error('Usage: node --experimental-strip-types tools/rea_boot_pilot.mjs /path/to/rea-checkout');
const sourceRoot=resolve(process.argv[2]);
const git=(args)=>spawnSync('git',['-C',sourceRoot,...args],{encoding:'utf8'});
const revision=git(['rev-parse','HEAD']);
if(revision.status!==0||revision.stdout.trim()!==commit)throw new Error('Pilot requires the pinned REA checkout '+commit);
if(git(['diff','--quiet','HEAD','--','src']).status!==0)throw new Error('Pilot requires unmodified REA source');
const sourceUrl=pathToFileURL(sourceRoot+'/').href;
registerHooks({resolve(specifier,context,nextResolve){
 // REA TypeScript imports refer to compiled .js files. Resolve those to the
 // original .ts files and let Node strip types; change no provider logic.
 if(context.parentURL?.startsWith(sourceUrl)&&specifier.startsWith('.')&&specifier.endsWith('.js'))
  specifier=specifier.slice(0,-3)+'.ts';
 return nextResolve(specifier,context);
}});
const {parseExecutableHeader}=await import(pathToFileURL(join(sourceRoot,'src/domain/binaryTarget.ts')).href);
const directory=join(root,'build/oracle/rea_pilot');
mkdirSync(directory,{recursive:true});
const sub=readFileSync(join(directory,'subcpu.bin'));
// Valid ELF32 big-endian EM_68K image: one read/execute PT_LOAD mapping the
// unchanged ROM at address zero, entry $402. No byte rewriting or spoofing
// an x86 architecture to get past REA's target checks.
const elf=Buffer.alloc(0x1000+sub.length);
elf.set([0x7f,0x45,0x4c,0x46,1,2,1]);
elf.writeUInt16BE(2,16);elf.writeUInt16BE(4,18);elf.writeUInt32BE(1,20);
elf.writeUInt32BE(0x402,24);elf.writeUInt32BE(52,28);
elf.writeUInt16BE(52,40);elf.writeUInt16BE(32,42);elf.writeUInt16BE(1,44);
for(const [offset,value] of [[52,1],[56,0x1000],[60,0],[64,0],[68,sub.length],[72,sub.length],[76,5],[80,0x1000]])elf.writeUInt32BE(value,offset);
sub.copy(elf,0x1000);writeFileSync(join(directory,'subcpu_m68k.elf'),elf);
const tests=[
 ['actual-sub-ROM',sub,false,'unsupported binary format'],
 ['actual-main-ROM',readFileSync(join(directory,'maincpu.bin')),false,'unsupported binary format'],
 ['actual-sub-ROM-m68k-ELF-wrapper',elf,false,'unsupported ELF architecture'],
 ['built-game-Mach-O-control',readFileSync(join(root,'afterburner2')),true,null]
];
const results=tests.map(([name,bytes,expectedOk,expectedError])=>{
 const r=parseExecutableHeader(bytes,process.arch,bytes.length);
 const pass=r.ok===expectedOk&&(r.ok||r.error===expectedError);
 if(!pass)throw new Error(`${name}: unexpected admission result`);
 return {name,accepted:r.ok,result:r.ok?r.value:r.error,expectation_passed:pass};
});
writeFileSync(join(directory,'admission.json'),JSON.stringify({source_commit:commit,
 node:process.version,scope:'unmodified REA executable-header parser; no CLI/provider/decompiler invoked',results},null,2)+'\n');
for(const r of results)console.log(r.name+': '+(r.accepted?'accepted':r.result));
console.log('Pilot expectations: 4/4 passed; ROM analysis admissions: 0/3 accepted');
