"""Build isolated ARMv4 CE GLES DLLs; leave canonical guest builder untouched."""
from pathlib import Path
import hashlib,inspect,json,shutil,struct,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent;EXP=HERE.parents[2];PROJECT=EXP.parents[1]
sys.path.insert(0,str(EXP/'guest/network-setup'));import build as base
TABLES={p:json.loads((EXP/f'prototypes/gles-android/guest/data/libgles_{p}-ordinals.json').read_text())['ordinals'] for p in ('cl','cm')}
ARITIES={
'eglGetDisplay':1,'eglInitialize':3,'eglChooseConfig':5,'eglGetConfigs':4,'eglGetConfigAttrib':4,
'eglCreateWindowSurface':4,'eglCreatePbufferSurface':3,'eglCreateContext':4,'eglMakeCurrent':4,
'eglDestroyContext':2,'eglDestroySurface':2,'eglTerminate':1,'eglGetError':0,'eglGetCurrentContext':0,
'eglGetCurrentDisplay':0,'eglGetCurrentSurface':1,'eglQuerySurface':4,'eglQueryString':2,'eglGetProcAddress':1,
'eglSwapBuffers':2,'eglSwapInterval':2,'eglWaitGL':0,'eglWaitNative':1,
'glGenTextures':2,'glGetString':1,'glGetError':0,'glGetIntegerv':2,'glReadPixels':7,'glFlush':0,'glFinish':0,
'glVertexPointer':4,'glColorPointer':4,'glTexCoordPointer':4,'glNormalPointer':3,'glClientActiveTexture':1,
'glEnableClientState':1,'glDisableClientState':1,'glLoadMatrixx':1,'glMultMatrixx':1,'glLoadMatrixf':1,'glMultMatrixf':1,
'glFogxv':2,'glFogfv':2,'glDeleteTextures':2,'glTexImage2D':9,'glDrawElements':4,'glDrawArrays':3,
'glPixelStorei':2,'glMatrixMode':1,'glShadeModel':1,'glViewport':4,'glClear':1,'glClearColorx':4,'glClearColor':4,
'glActiveTexture':1,'glTexEnvx':3,'glTexEnvf':3,'glTexParameterx':3,'glTexParameterf':3,'glFogx':2,'glFogf':2,
'glBindTexture':2,'glAlphaFuncx':2,'glAlphaFunc':2,'glDisable':1,'glEnable':1,'glLoadIdentity':0,
'glHint':2,'glFrustumx':6,'glFrustumf':6,'glFrontFace':1,'glBlendFunc':2,'glColor4x':4,'glColor4f':4,
'glClearDepthx':1,'glClearDepthf':1,'glDepthFunc':1,'glDepthMask':1,'glDepthRangex':2,'glDepthRangef':2,
'glCullFace':1,'glScissor':4,'glPushMatrix':0,'glPopMatrix':0,'glTranslatex':3,'glTranslatef':3,
'glRotatex':4,'glRotatef':4,'glScalex':3,'glScalef':3,'glOrthox':6,'glOrthof':6,
}
CM={v:int(k) for k,v in TABLES['cm'].items()}
def aligned_exports(pe,dll,table):
    """LLD ARMNT permits unaligned EAT arrays; ARM920T CE requires alignment."""
    pe=bytearray(pe);header=struct.unpack_from('<I',pe,60)[0];opt=header+24
    count=struct.unpack_from('<H',pe,header+6)[0];first=opt+struct.unpack_from('<H',pe,header+20)[0]
    sections=[struct.unpack_from('<8sIIIIIIHHI',pe,first+i*40) for i in range(count)]
    def rva(v):
        for s in sections:
            if s[2]<=v<s[2]+max(s[1],s[3]):return s[4]+v-s[2]
        raise ValueError(hex(v))
    old=struct.unpack_from('<I',pe,opt+96)[0];vals=struct.unpack_from('<IIHHIIIIIII',pe,rva(old))
    funcs=[struct.unpack_from('<I',pe,rva(vals[8])+i*4)[0]&~1 for i in range(vals[6])]
    new_rva=base.align(max(s[2]+max(s[1],s[3]) for s in sections),4096)
    data=bytearray(40)
    def add(content,alignment=4):
        data.extend(bytes(base.align(len(data),alignment)-len(data)));at=len(data);data.extend(content);return new_rva+at
    name=add(dll.encode()+b'\0');functions=add(struct.pack('<'+'I'*len(funcs),*funcs))
    sorted_names=sorted((n,int(ord)-vals[5]) for ord,n in table.items())
    names=add(bytes(4*len(sorted_names)));ordinals=add(struct.pack('<'+'H'*len(sorted_names),*(i for n,i in sorted_names)))
    for i,(n,idx) in enumerate(sorted_names):
        nrva=add(n.encode()+b'\0',1);struct.pack_into('<I',data,names-new_rva+i*4,nrva)
    struct.pack_into('<IIHHIIIIIII',data,0,0,0,0,0,name,vals[5],len(funcs),len(sorted_names),functions,names,ordinals)
    # Export name packing can also leave LLD's following import descriptors
    # unaligned. CE's ARM920T loader reads their DWORD fields directly.
    export_size=len(data)
    import_rva,import_size=struct.unpack_from('<II',pe,opt+104)
    if import_rva:
        descriptors=bytes(pe[rva(import_rva):rva(import_rva)+import_size])
        aligned_import=add(descriptors,4)
        struct.pack_into('<II',pe,opt+104,aligned_import,import_size)
    old_headers=struct.unpack_from('<I',pe,opt+60)[0];new_headers=base.align(first+(count+1)*40,512);growth=new_headers-old_headers
    if growth:
        pe[old_headers:old_headers]=bytes(growth)
        for i,s in enumerate(sections):struct.pack_into('<I',pe,first+i*40+20,s[4]+growth if s[4] else 0)
        struct.pack_into('<I',pe,opt+60,new_headers)
    raw=base.align(len(pe),512);rawsize=base.align(len(data),512);pe.extend(bytes(raw-len(pe)));pe.extend(data);pe.extend(bytes(rawsize-len(data)))
    struct.pack_into('<8sIIIIIIHHI',pe,first+count*40,b'.edata\0\0',len(data),new_rva,rawsize,raw,0,0,0,0,0x40000040)
    struct.pack_into('<H',pe,header+6,count+1);struct.pack_into('<II',pe,opt+96,new_rva,export_size);struct.pack_into('<I',pe,opt+56,base.align(new_rva+len(data),4096))
    struct.pack_into('<I',pe,opt+8,struct.unpack_from('<I',pe,opt+8)[0]+rawsize)
    return pe
def make_sources(out):
    out.mkdir(parents=True,exist_ok=True)
    (out/'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n,op in CM.items())+'static const unsigned char op_supported[141]={'+','.join('1' if i>=35 else '0' for i in range(141))+'};\n')
    _,addresses,_,_=base.imports()
    asm='.syntax unified\n.arch armv4t\n.arm\n.text\n'
    for n in base.IMPORTS:
        asm+=f'.global {n}\n.type {n},%function\n{n}:\n ldr ip,[pc,#4]\n ldr ip,[ip]\n bx ip\n .word __iat_{n}\n'
    for n,op in CM.items():
        asm+=f'.global {n}\n.type {n},%function\n{n}:\n push {{r0-r3,r4,lr}}\n mov r0,#{op}\n mov r1,sp\n add r2,sp,#24\n mov r3,#{ARITIES.get(n,0)}\n bl bridge\n ldr r4,[sp,#16]\n ldr lr,[sp,#20]\n add sp,sp,#24\n bx lr\n'
    asm+='\n.global __aeabi_uidivmod\n.type __aeabi_uidivmod,%function\n__aeabi_uidivmod:\n push {r1-r3,lr}\n str r0,[sp,#4]\n bl __aeabi_uidiv\n pop {r1-r3,lr}\n mul r3,r0,r1\n sub r1,r2,r3\n bx lr\n'
    (out/'stubs.S').write_text(asm)
    (out/'bridge.ld').write_text('ENTRY(_start)\nSECTIONS { . = 0x11000; .p2d : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) } }\n'+''.join('__iat_'+name+' = '+hex(value)+';\n' for name,value in addresses.items()))
    objects=[]
    for i,path in enumerate([HERE/'guest_bridge.c',out/'stubs.S']):
        obj=out/f'{i}.o'
        subprocess.run(['clang-17','--target=armv4t-none-eabi','-mcpu=arm920t','-marm','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fshort-wchar','-I'+str(out),'-c',str(path),'-o',str(obj)],check=True)
        objects.append(obj)
    elf=out/'bridge.elf'
    subprocess.run(['ld.lld-17','--emit-relocs','--no-undefined','-T',str(out/'bridge.ld'),*map(str,objects),'-o',str(elf)],check=True)
    return elf.read_bytes()

def symbols(elf):
    named,sections=base.elf_sections(elf);tab=named['.symtab'];strings=sections[tab[6]];strings=elf[strings[4]:strings[4]+strings[5]];out={}
    for pos in range(tab[4],tab[4]+tab[5],16):
        name,value,size,info,other,index=struct.unpack_from('<IIIBBH',elf,pos)
        if index and name:
            key=strings[name:].split(b'\0',1)[0].decode();out[key]=value
    return out

def build(out):
    elf=make_sources(out);syms=symbols(elf);reports={}
    original=inspect.getsource(base.link_coff)
    for profile,table in TABLES.items():
        dll='libGLES_'+profile.upper()+'.dll';exports={n:syms[n]-(base.BASE+base.CODE) for n in table.values()}
        definition=out/(dll+'.def');definition.write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join(f'{n} @{ord}\n' for ord,n in table.items()))
        source=original.replace("symbol('_start', entry, 1)","symbol('_start', entry, 1)\n    for n, v in export_symbols.items(): symbol(n, v, 1)")
        source=source.replace("[linker, '/machine:arm', '/entry:_start'","[linker, '/machine:arm', '/dll', '/def:'+str(export_definition), '/entry:_start'")
        assert source!=original and "export_symbols.items()" in source and "'/dll'" in source
        env=dict(base.__dict__,export_symbols=exports,export_definition=definition)
        exec(compile(source,str(HERE/'build_guest.py'),'exec'),env)
        old=base.link_coff;base.link_coff=env['link_coff']
        try:pe=bytearray(base.pack_elf(elf))
        finally:base.link_coff=old
        header=struct.unpack_from('<I',pe,60)[0];opt=header+24;sec=opt+struct.unpack_from('<H',pe,header+20)[0]
        sections=[struct.unpack_from('<8sIIIIIIHHI',pe,sec+i*40) for i in range(struct.unpack_from('<H',pe,header+6)[0])]
        def rva(v):
            for s in sections:
                if s[2]<=v<s[2]+max(s[1],s[3]):return s[4]+v-s[2]
            raise ValueError(hex(v))
        ex,_=struct.unpack_from('<II',pe,opt+96);vals=struct.unpack_from('<IIHHIIIIIII',pe,rva(ex));num,functions=vals[6],vals[8]
        for i in range(num):
            at=rva(functions)+4*i;v=struct.unpack_from('<I',pe,at)[0]
            if v:struct.pack_into('<I',pe,at,v&~1)
        pe=aligned_exports(pe,dll,table)
        (out/dll).write_bytes(pe)
        reports[dll]={'sha256':hashlib.sha256(pe).hexdigest(),'bytes':len(pe),'exports':len(exports),'profile':profile}
    (out/'manifest.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports,indent=2))

if __name__=='__main__':build(HERE/'guest-build')
