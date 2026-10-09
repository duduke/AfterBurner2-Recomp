#!/usr/bin/env node
// Offline analysis only: upstream tooling and all ROM-derived output stay local.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { resolve, join } from 'node:path';

const commit='2ff52822f0d706f9f39762e079f300f45cfd451d';
if(process.argv.length!==3) throw new Error('Usage: node tools/z80_analysis_pilot.mjs /path/to/z80dismblr-checkout (npm ci && npm run compile there first)');
const upstream=resolve(process.argv[2]);
const run=(cmd,args)=>{
 const r=spawnSync(cmd,args,{encoding:'utf8'});
 if(r.status!==0) throw new Error(`${cmd} failed: ${r.stderr || r.stdout}`);
 return r.stdout;
};
if(run('git',['-C',upstream,'rev-parse','HEAD']).trim()!==commit) throw new Error('Wrong upstream revision');
run('git',['-C',upstream,'diff','--exit-code','HEAD','--','src','package.json','package-lock.json']);
run(process.execPath,[join(upstream,'node_modules/typescript/bin/tsc'),'-p',join(upstream,'src')]);
const dir=resolve('build/oracle/z80_pilot');
mkdirSync(dir,{recursive:true});
// Match the board map; the upper 4 KB is PCM/RAM, not executable ROM.
const rom=readFileSync('roms/epr-11112.17').subarray(0,0xf000);
writeFileSync(join(dir,'sound.bin'),rom);
const trace=readFileSync(join(dir,'sound.tr'),'utf8');
const capture=JSON.parse(readFileSync(join(dir,'sound.tr.json'),'utf8'));
if(!capture.complete || !Number.isInteger(capture.frames) || capture.frames<1) throw new Error('Incomplete trace capture');
const rows=trace.split('\n').flatMap(line=>{
 const m=/^([0-9A-Fa-f]{4}):\s*(.+)$/.exec(line);
 return m?[{pc:parseInt(m[1],16),asm:m[2].trim()}]:[];
});
if(!rows.length || rows[0].pc!==0) throw new Error('Missing reset-entry MAME trace');
if(rows.some(r=>r.pc>=rom.length)) throw new Error('Trace executes outside static ROM: needs runtime memory capture');
for(const mode of ['static','traced']) {
 const args=[join(upstream,'out/z80dismblr.js'),'--bin','0',join(dir,'sound.bin')];
 args.push(...(mode==='static'?['--codelabel','0','RESET']:['--tr',join(dir,'sound.tr')]));
 args.push('--out',join(dir,mode+'.list'),'--callgraphout',join(dir,mode+'.dot'),
  '--flowchartout',join(dir,mode+'-reset.dot'),'--flowchartaddresses','0');
 writeFileSync(join(dir,mode+'.log'),run(process.execPath,args));
}
// Compare independently recorded MAME mnemonics and operands, ignoring only
// whitespace/case and equivalent numeric notation. Labels are not used here.
const require=createRequire(import.meta.url);
const {BaseMemory}=require(join(upstream,'out/disassembler/basememory.js'));
const {Opcode}=require(join(upstream,'out/disassembler/opcode.js'));
const memory=new BaseMemory(0,rom);
const normal=s=>s.toLowerCase().replace(/\$([0-9a-f]+)/g,(_,v)=>parseInt(v,16))
 .replace(/\b([0-9a-f]+)h\b/g,(_,v)=>parseInt(v,16)).replace(/\s+/g,'');
const unique=new Map();
for(const r of rows) {
 if(unique.has(r.pc) && unique.get(r.pc)!==r.asm) throw new Error('Changing instruction at '+r.pc.toString(16));
 unique.set(r.pc,r.asm);
}
const differences=[];
for(const [pc,asm] of unique) {
 const decoded=Opcode.getOpcodeAt(memory,pc).disassemble().mnemonic;
 if(normal(decoded)!==normal(asm)) differences.push({pc:pc.toString(16),mame:asm,upstream:decoded});
}
const mutated=Buffer.from(rom);
mutated[0]^=8; // DI -> EI, private in-memory negative control only.
const mutationRejected=normal(Opcode.getOpcodeAt(new BaseMemory(0,mutated),0).disassemble().mnemonic)!==normal(unique.get(0));
if(!mutationRejected) throw new Error('Decoder comparison accepted the reset mutation');
const listed=mode=>new Set(readFileSync(join(dir,mode+'.list'),'utf8').split('\n').flatMap(line=>{
 const m=/^([0-9A-F]{4})\s{2,}(\S+)/.exec(line);
 return m && !['defb','defw','defs'].includes(m[2].toLowerCase())?[parseInt(m[1],16)]:[];
}));
const staticPCs=listed('static'), tracedPCs=listed('traced');
const graphNodes=mode=>(readFileSync(join(dir,mode+'.dot'),'utf8').match(/\[label=/g)||[]).length;
const result={upstream_commit:commit,node:process.version,capture,
 scope:'Read-only MAME trace with sound disabled; static ROM analysis, no timing or execution acceptance',
 instructions:rows.length,unique_pcs:unique.size,decode_identical:unique.size-differences.length,decode_differences:differences,
 observed_missing_static:[...unique.keys()].filter(pc=>!staticPCs.has(pc)).map(pc=>pc.toString(16)),
 observed_missing_traced:[...unique.keys()].filter(pc=>!tracedPCs.has(pc)).map(pc=>pc.toString(16)),
 static_instruction_starts:staticPCs.size,traced_instruction_starts:tracedPCs.size,
 static_graph_nodes:graphNodes('static'),traced_graph_nodes:graphNodes('traced'),reset_DI_to_EI_mutation_rejected:mutationRejected,
 trace_added_starts:[...tracedPCs].filter(pc=>!staticPCs.has(pc)).map(pc=>pc.toString(16)),
 indirect_jumps:rows.filter(r=>normal(r.asm)==='jp(hl)').length,
 interrupt_returns:rows.filter(r=>['reti','retn'].includes(normal(r.asm))).length};
writeFileSync(join(dir,'analysis.json'),JSON.stringify(result,null,2)+'\n');
console.log(`MAME trace: ${rows.length} instructions, ${unique.size} unique PCs`);
console.log(`Decoder: ${result.decode_identical}/${unique.size} identical, ${differences.length} differences, 0 skipped`);
console.log(`Observed PCs missing: static ${result.observed_missing_static.length}, traced ${result.observed_missing_traced.length}; trace added ${result.trace_added_starts.length} instruction starts`);
console.log(`Observed indirect jumps ${result.indirect_jumps}, interrupt returns ${result.interrupt_returns}`);
if(differences.length || result.observed_missing_traced.length) process.exitCode=1;
