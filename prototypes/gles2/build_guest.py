"""Separate CE DLL build; reuse bounded PE tooling without editing GLES1."""
from pathlib import Path
import ast,hashlib,importlib.util,inspect,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parent;BASE=ROOT.parents[1];LAB=ROOT.parent/'gles-android'

def load_base(thin):
 spec=importlib.util.spec_from_file_location('ce2_build',BASE/'guest/network-setup/build.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
 if thin:
  base.IMPORTS=('p2d2_egl_dispatch',)
  def imports():
   data=bytearray(40)
   def add(b,alignment=1):
    data.extend(bytes(base.align(len(data),alignment)-len(data)));at=len(data);data.extend(b);return base.IDATA+at
   dll=add(b'libGLESv2.dll\0');named=[add(b'\0\0'+n.encode()+b'\0',2) for n in base.IMPORTS];thunks=struct.pack('<'+str(len(named)+1)+'I',*named,0);ilt=add(thunks,4);iat=add(thunks,4);struct.pack_into('<5I',data,0,ilt,0,0,dll,iat)
   return bytes(data),{n:base.BASE+iat+i*4 for i,n in enumerate(base.IMPORTS)},iat,len(thunks)
  base.imports=imports
 return base

def symbols(base,elf):
 named,sections=base.elf_sections(elf);tab=named['.symtab'];s=sections[tab[6]];strings=elf[s[4]:s[4]+s[5]];out={}
 for p in range(tab[4],tab[4]+tab[5],16):
  name,value,size,info,other,index=struct.unpack_from('<IIIBBH',elf,p)
  if name and index:out[strings[name:].split(b'\0',1)[0].decode()]=value
 return out

def aligned(base,pe,dll,table):
 # Use the proven alignment repair verbatim, with this private builder's base.
 path=LAB/'guest/build_bridge.py';src=path.read_text();node=next(n for n in ast.parse(src).body if isinstance(n,ast.FunctionDef) and n.name=='aligned_exports');fn='\n'.join(src.splitlines()[node.lineno-1:node.end_lineno]);env={'base':base,'struct':struct};exec(fn,env);return bytes(env['aligned_exports'](pe,dll,table))

def pe_info(b):
 pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;base=struct.unpack_from('<I',b,opt+28)[0];first=opt+struct.unpack_from('<H',b,pe+20)[0];ss=[struct.unpack_from('<8sIIII',b,first+i*40) for i in range(struct.unpack_from('<H',b,pe+6)[0])]
 def at(rva):return next(off+rva-va for name,vs,va,size,off in ss if va<=rva<va+max(vs,size))
 def string(rva):return b[at(rva):].split(b'\0',1)[0].decode()
 ex=struct.unpack_from('<I',b,opt+96)[0];v=struct.unpack_from('<IIHH7I',b,at(ex));names={}
 for i in range(v[7]):
  n=string(struct.unpack_from('<I',b,at(v[9])+4*i)[0]);idx=struct.unpack_from('<H',b,at(v[10])+2*i)[0];names[n]=base+struct.unpack_from('<I',b,at(v[8])+4*idx)[0]
 dispatcher=0;imp,size=struct.unpack_from('<II',b,opt+104)
 for d in range(at(imp),at(imp)+size,20):
  oft,t,f,n,ft=struct.unpack_from('<5I',b,d)
  if not n:break
  k=0
  while True:
   item=struct.unpack_from('<I',b,at(oft or ft)+k*4)[0]
   if not item:break
   if not item&0x80000000 and string(item+2)=='p2d2_egl_dispatch':dispatcher=base+ft+k*4
   k+=1
 return names,dispatcher

def build_one(thin,apis):
 base=load_base(thin);dll='libEGL.dll' if thin else 'libGLESv2.dll';out=ROOT/'guest/build'/('egl' if thin else 'core');out.mkdir(parents=True,exist_ok=True)
 selected=[a for a in apis if not thin or a['kind']=='egl'];asm='.syntax unified\n.arch armv4t\n.arm\n.text\n'
 for n in base.IMPORTS:asm+=f'.global {n}\n.type {n},%function\n{n}:\n ldr ip,[pc,#4]\n ldr ip,[ip]\n bx ip\n .word __iat_{n}\n'
 for a in selected:
  n=a['name'];op=a['op'];asm+=f'.global {n}\n.type {n},%function\n{n}:\n push {{r0-r3,r4,lr}}\n mov r0,#0x20000\n'
  if op&0xff00:asm+=f' orr r0,r0,#{op&0xff00}\n'
  if op&255:asm+=f' orr r0,r0,#{op&255}\n'
  asm+=f' mov r1,sp\n add r2,sp,#24\n mov r3,#{a["arity"]}\n bl '+('p2d2_egl_dispatch' if thin else 'bridge2')+'\n ldr r4,[sp,#16]\n ldr lr,[sp,#20]\n add sp,sp,#24\n bx lr\n'
 if not thin:asm+='\n.global __aeabi_uidivmod\n.type __aeabi_uidivmod,%function\n__aeabi_uidivmod:\n push {r1-r3,lr}\n str r0,[sp,#4]\n bl __aeabi_uidiv\n pop {r1-r3,lr}\n mul r3,r0,r1\n sub r1,r2,r3\n bx lr\n'
 (out/'stubs.S').write_text(asm)
 if thin:(out/'entry.c').write_text('int _start(void*m,unsigned reason,void*r){(void)m;(void)reason;(void)r;return 1;}\n')
 src=[out/'entry.c'] if thin else [ROOT/'guest'/n for n in ('transport.c','commands.c','egl.c','bridge.c','memory.c')]+[LAB/'guest/presentation.c']
 objects=[]
 for i,p in enumerate([*src,out/'stubs.S']):
  obj=out/f'{i}.o';subprocess.run(['clang-17','--target=armv4t-none-eabi','-mcpu=arm920t','-marm','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fshort-wchar','-I'+str(ROOT),'-c',str(p),'-o',str(obj)],check=True);objects.append(obj)
 script='ENTRY(_start)\nSECTIONS{. = 0x11000; .p2d : {*(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) . = ALIGN(4);} /DISCARD/ : {*(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*)}}\n'
 script+=''.join('__iat_'+n+' = '+hex(v)+';\n' for n,v in base.imports()[1].items());(out/'bridge.ld').write_text(script)
 subprocess.run(['ld.lld-17','--emit-relocs','--no-undefined','-T',str(out/'bridge.ld'),*map(str,objects),'-o',str(out/'bridge.elf')],check=True)
 elf=(out/'bridge.elf').read_bytes();syms=symbols(base,elf);names=[a['name'] for a in selected]+([] if thin else ['p2d2_egl_dispatch']);table={str(i+1 if thin else a['export_ordinal']):a['name'] for i,a in enumerate(selected)};table.update({} if thin else {'106':'p2d2_egl_dispatch'});exports={n:syms[n]-0x11000 for n in names};definition=out/'exports.def';definition.write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join(n+' @'+o+'\n' for o,n in table.items()))
 source=inspect.getsource(base.link_coff).replace("symbol('_start', entry, 1)","symbol('_start', entry, 1)\n    for n,v in export_symbols.items(): symbol(n,v,1)")
 source=source.replace("[linker, '/machine:arm', '/entry:_start'","[linker, '/machine:arm', '/dll', '/def:'+str(export_definition), '/entry:_start'")
 if thin:source=source.replace('LIBRARY coredll.dll','LIBRARY libGLESv2.dll').replace("name+' @'+str(ORDINALS[name])+' NONAME\\n'","name+'\\n'")
 env=dict(base.__dict__,export_symbols=exports,export_definition=definition);exec(compile(source,str(Path(__file__)), 'exec'),env);base.link_coff=env['link_coff'];pe=aligned(base,base.pack_elf(elf),dll,table);dest=ROOT/'guest/build'/dll;dest.write_bytes(pe);export_va,iat=pe_info(pe)
 bridge_va=0x200000 if thin else export_va['p2d2_egl_dispatch']+syms['bridge2']-syms['p2d2_egl_dispatch']
 return dll,{'bytes':len(pe),'sha256':hashlib.sha256(pe).hexdigest(),'exports_va':export_va,'bridge_va':bridge_va,'dispatcher_iat_va':iat,'elf_code_bytes':base.elf_sections(elf)[0]['.p2d'][5]}

def main():
 apis=json.loads((ROOT/'api.json').read_text())['apis'];report=dict(build_one(thin,apis) for thin in (False,True));(ROOT/'guest/build/manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({n:{k:v for k,v in r.items() if k!='exports_va'} for n,r in report.items()},indent=2))
if __name__=='__main__':main()
