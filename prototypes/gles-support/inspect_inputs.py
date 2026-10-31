"""Read-only PE metadata parser for authored guest DLL verification."""
import re,struct

def pe(data):
    if data[:2] != b'MZ': return None
    off = struct.unpack_from('<I', data, 60)[0]
    if data[off:off+4] != b'PE\0\0': return None
    machine, count, stamp, _, _, optsize, flags = struct.unpack_from('<HHIIIHH', data, off+4)
    opt = off+24
    if struct.unpack_from('<H', data, opt)[0] != 0x10b: return {'machine':hex(machine), 'not_pe32':True}
    base = struct.unpack_from('<I', data, opt+28)[0]
    subsystem = struct.unpack_from('<H', data, opt+68)[0]
    sections=[]
    for i in range(count):
        pos=opt+optsize+40*i
        name=data[pos:pos+8].split(b'\0')[0].decode('ascii','replace')
        vs, va, size, raw=struct.unpack_from('<IIII',data,pos+8)
        sections.append((name,vs,va,size,raw))
    def rva(addr):
        for _,vs,va,size,raw in sections:
            if va <= addr < va+max(vs,size): return raw+addr-va
        if addr < struct.unpack_from('<I',data,opt+60)[0]: return addr
        raise ValueError('Unmapped RVA '+hex(addr))
    def cstr(addr):
        at=rva(addr);return data[at:data.index(b'\0',at)].decode('ascii','replace')
    imports=[]
    imp, length=struct.unpack_from('<II',data,opt+104)
    if imp:
        at=rva(imp)
        for i in range(128):
            ilt,ts,chain,name,iat=struct.unpack_from('<IIIII',data,at+20*i)
            if not any((ilt,ts,chain,name,iat)):break
            funcs=[]; th=rva(ilt or iat)
            for j in range(4096):
                v=struct.unpack_from('<I',data,th+4*j)[0]
                if not v:break
                funcs.append('#'+str(v&0xffff) if v&0x80000000 else cstr(v+2))
            imports.append({'dll':cstr(name),'symbols':funcs})
    exports=[]
    exp,_=struct.unpack_from('<II',data,opt+96)
    if exp:
        vals=struct.unpack_from('<IIHHIIIIIII',data,rva(exp))
        ordinal,nfunc,nnames,functions,names,ordinals=vals[5:]
        named={}
        for i in range(nnames):
            name=struct.unpack_from('<I',data,rva(names)+4*i)[0]
            idx=struct.unpack_from('<H',data,rva(ordinals)+2*i)[0]
            named[idx]=cstr(name)
        for i in range(nfunc):
            addr=struct.unpack_from('<I',data,rva(functions)+4*i)[0]
            if addr:exports.append({'ordinal':ordinal+i,'name':named.get(i),'rva':hex(addr)})
    strings=[x.decode('ascii','replace') for x in re.findall(rb'[\x20-\x7e]{7,}',data)]
    strings += [x.decode('utf-16le','replace') for x in re.findall(rb'(?:[\x20-\x7e]\0){7,}',data)]
    selected=[s for s in strings if re.search(r'(?i)powervr|vincent|software|hardware|opengl|imagination|nvidia|2700|go.?force|renderer|aspyr|ionfx|libgles|libegl|\.dll|\.exe',s)]
    return dict(machine=hex(machine),architecture={0x14c:'x86',0x1c0:'ARM',0x1c2:'ARM/Thumb',0x1c4:'ARMv7'}.get(machine,'unknown'),
                subsystem=subsystem,subsystem_name={2:'Windows GUI',9:'Windows CE GUI'}.get(subsystem,'other'),
                image_base=hex(base),timestamp=stamp,imports=imports,exports=exports,interesting_strings=selected,
                sections=[dict(name=s[0],virtual_size=s[1],rva=hex(s[2]),raw_size=s[3]) for s in sections])
