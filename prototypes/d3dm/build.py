"""Isolated CE adapter tools; supplied SDK headers remain byte-identical."""
from pathlib import Path
import argparse
import os
import hashlib
import importlib.util
import inspect
import json
import re
import shutil
import struct
import subprocess

HERE = Path(__file__).resolve().parent
SDK = Path(os.environ.get('P2D_D3DM_SDK', str(HERE/'external-sdk-not-distributed')))
HEADERS = {
    '_3d3dm_h': ('d3dm.h', '9000237dd02ce751593ff6af888032b2ee684744d7807012ea3ebdb96e254b95'),
    '_3D3DMTY_1_H': ('d3dmtypes.h', 'dc08ab1690f889b8085ddd62ad454461196005fdd353f52dc722f0135ebc9a62'),
    '_3d3dmcaps_h': ('d3dmcaps.h', '3b67e9e5f7fe2258a3811ac046687d3c1d203ddbfd75a7b5b76c296007bedf28'),
}


def prepare_headers(out):
    include = Path(out)/'include'
    include.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for source, (name, expected) in HEADERS.items():
        header=SDK/source
        if not header.exists():header=SDK/name
        if not header.is_file():raise RuntimeError('Supply the matching Direct3D Mobile SDK headers via P2D_D3DM_SDK; SDK headers are not distributed in this repository')
        data = header.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise ValueError('Official SDK header identity changed: '+source)
        (include/name).write_bytes(data)
        hashes[name] = actual
    shutil.copyfile(HERE/'guest/ce_sdk_compat.h', include/'windows.h')
    return include, hashes


def check_abi(out):
    out = Path(out)
    include, hashes = prepare_headers(out)
    targets = {'armv4t': ['--target=armv4t-none-eabi', '-mcpu=arm920t', '-marm'], 'host': []}
    for label, flags in targets.items():
        subprocess.run(['clang-17', *flags, '-std=c11', '-ffreestanding', '-fshort-wchar',
            '-Wall', '-Werror', '-I'+str(include), '-c', str(HERE/'tests/abi.c'),
            '-o', str(out/(label+'-abi.o'))], check=True)
    report = dict(sdk=120, sdk_hashes=hashes, compiled_targets=list(targets))
    (out/'ABI.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def generate_vtables(out):
    """Generate correctly typed unavailable slots directly from the official ABI."""
    header_path=SDK/'_3d3dm_h'
    if not header_path.exists():header_path=SDK/'d3dm.h'
    header=header_path.read_text()
    overrides = {
        'IDirect3DMobile': ('f', {'QueryInterface','AddRef','Release','GetAdapterIdentifier','GetAdapterCount',
            'GetAdapterModeCount','EnumAdapterModes','GetAdapterDisplayMode',
            'CheckDeviceType','CheckDeviceFormat','CheckDepthStencilMatch','GetDeviceCaps','CreateDevice'}),
        'IDirect3DMobileDevice': ('d', {'QueryInterface','AddRef','Release','GetDirect3D',
            'GetDeviceCaps','GetDisplayMode','GetCreationParameters','ValidateDevice','TestCooperativeLevel','BeginScene',
            'EndScene','Clear','Present','CreateVertexBuffer','CreateIndexBuffer',
            'CreateTexture','SetStreamSource','GetStreamSource','SetIndices',
            'GetIndices','SetTexture','GetTexture','UpdateTexture','SetRenderState','GetRenderState','SetTransform','GetTransform','SetViewport','GetViewport','SetTextureStageState','GetTextureStageState','SetMaterial','GetMaterial','SetLight','GetLight','LightEnable','GetLightEnable','DrawPrimitive','DrawIndexedPrimitive','GetBackBuffer','GetRenderTarget','CreateImageSurface','CopyRects','Reset'}),
    }
    lines = ['/* Generated from hash-verified SDK120 method declarations. */']
    for interface, (prefix, implemented) in overrides.items():
        body = header.split('DECLARE_INTERFACE_('+interface+',',1)[1].split('};',1)[0]
        entries = []
        for match in re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\((.*?)\)\s*PURE;',body,re.S):
            result, named, ordinary, args = match.groups()
            result, name = (result or 'HRESULT').strip(), named or ordinary
            if name in implemented:
                function = prefix+'_'+name
            else:
                function = 'unavailable_'+prefix+'_'+name
                args = args.replace('THIS_',interface+' *This,').replace('THIS',interface+' *This')
                zero = ''.join('if('+p+')*'+p+'=0;' for p in re.findall(r'\*\*\s*(\w+)',args))
                ret = 'return p2d_failure("'+name+'",D3DMERR_NOTAVAILABLE);' if result=='HRESULT' else 'return 0;' if result!='void' else ''
                lines.append('static '+result+' '+function+'('+args+'){'+zero+ret+'}')
            if name in implemented and result=='HRESULT':
                signature=args.replace('THIS_',interface+' *This,').replace('THIS',interface+' *This')
                call=','.join(re.search(r'(\w+)\s*$',p.strip()).group(1) for p in signature.split(','))
                wrapper='checked_'+prefix+'_'+name
                detail=''
                if name=='CheckDeviceFormat':
                    detail='if(FAILED(hr)){static UINT logged;if(logged++<8){'+''.join('p2d_failure("ARG.'+arg+'",(HRESULT)(uintptr_t)'+arg+');' for arg in ['AdapterFormat','Usage','RType','CheckFormat'])+'}}'
                success='static BOOL called;if(!called){called=1;p2d_failure("CALL.'+name+'",hr);}'
                lines.append('#ifdef P2D_D3DM_DIAGNOSTIC\nstatic HRESULT '+wrapper+'('+signature+'){HRESULT hr='+function+'('+call+');'+success+detail+'return FAILED(hr)?p2d_failure("ERR.'+name+'",hr):hr;}\n#define '+wrapper+' '+wrapper+'\n#else\n#define '+wrapper+' '+function+'\n#endif')
                function=wrapper
            entries.append('.'+name+'='+function)
        expected = 17 if prefix=='f' else 62
        if len(entries)!=expected:
            raise ValueError('Incomplete official vtable: '+interface)
        lines.append('static const '+interface+'Vtbl '+('factory' if prefix=='f' else 'device')+'_vtable={'+','.join(entries)+'};')
    (Path(out)/'vtables.inc').write_text('\n'.join(lines)+'\n')


def validate_tables(data):
    pe = struct.unpack_from('<I',data,60)[0]
    opt=pe+24
    first=opt+struct.unpack_from('<H',data,pe+20)[0]
    sections=[struct.unpack_from('<8sIIII',data,first+i*40)
              for i in range(struct.unpack_from('<H',data,pe+6)[0])]
    def at(rva):
        return next(raw+rva-va for name,vs,va,size,raw in sections if va<=rva<va+max(vs,size))
    export=struct.unpack_from('<I',data,opt+96)[0]
    imports,size=struct.unpack_from('<II',data,opt+104)
    assert export%4==0 and imports%4==0
    fields=struct.unpack_from('<IIHH7I',data,at(export))
    assert fields[8]%4==0 and fields[9]%4==0 and fields[10]%2==0
    for i in range(fields[6]):
        assert struct.unpack_from('<I',data,at(fields[8])+4*i)[0]%4==0
    for pos in range(at(imports),at(imports)+size,20):
        ilt,stamp,forward,name,iat=struct.unpack_from('<5I',data,pos)
        if not name:break
        assert ilt%4==0 and iat%4==0
    return True


def build_adapter(out,diagnostic=True):
    out=Path(out)
    abi=check_abi(out)
    generate_vtables(out)
    # Reuse the tested CE ELF-to-PE tools without changing their outputs/globals.
    path=HERE.parent/'gles2/build_guest.py'
    spec=importlib.util.spec_from_file_location('d3dm_ce_pack_helpers',path)
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    base=helpers.load_base(False)
    asm='.syntax unified\n.arch armv4t\n.arm\n.text\n'
    for name in base.IMPORTS:
        asm+='.global '+name+'\n.type '+name+',%function\n'+name+':\n ldr ip,[pc,#4]\n ldr ip,[ip]\n bx ip\n .word __iat_'+name+'\n'
    (out/'imports.S').write_text(asm)
    sources=[HERE/'guest/adapter.c',HERE/'guest/resources.c',HERE/'guest/state.c',HERE/'guest/draw.c',HERE/'guest/pipeline.c',HERE/'guest/gles.c',out/'imports.S']
    (out/'shaders.inc').write_text(''.join('static const char p2d_'+label+'[]='+json.dumps((HERE/'guest'/name).read_text())+';\n' for label,name in [('vertex','d3dm.vert'),('fragment','d3dm.frag')]))
    objects=[]
    for i,source in enumerate(sources):
        obj=out/('dll-'+str(i)+'.o')
        subprocess.run(['clang-17','--target=armv4t-none-eabi','-mcpu=arm920t','-marm',
            '-Os',*(['-DP2D_D3DM_DIAGNOSTIC'] if diagnostic else []),'-ffreestanding','-fno-builtin','-fno-stack-protector',
            '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fshort-wchar',
            '-I'+str(out/'include'),'-I'+str(out),'-I'+str(HERE/'guest'),
            '-c',str(source),'-o',str(obj)],check=True)
        objects.append(obj)
    script='ENTRY(_start)\nSECTIONS{. = 0x11000; .p2d : {*(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) . = ALIGN(4);} /DISCARD/ : {*(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*)}}\n'
    script+=''.join('__iat_'+n+' = '+hex(v)+';\n' for n,v in base.imports()[1].items())
    (out/'dll.ld').write_text(script)
    subprocess.run(['ld.lld-17','--emit-relocs','--no-undefined','-T',str(out/'dll.ld'),
                    *map(str,objects),'-o',str(out/'dll.elf')],check=True)
    elf=(out/'dll.elf').read_bytes();syms=helpers.symbols(base,elf)
    table={'1':'Direct3DMobileCreate','2':'P2DD3DMAdapterVersion'}
    exports={name:syms[name]-0x11000 for name in table.values()}
    definition=out/'exports.def'
    definition.write_text('LIBRARY D3DM.dll\nEXPORTS\n'+''.join(n+' @'+o+'\n' for o,n in table.items()))
    original=inspect.getsource(base.link_coff)
    source=original.replace("symbol('_start', entry, 1)","symbol('_start', entry, 1)\n    for n,v in export_symbols.items(): symbol(n,v,1)")
    source=source.replace("[linker, '/machine:arm', '/entry:_start'", "[linker, '/machine:arm', '/dll', '/def:'+str(export_definition), '/entry:_start'")
    assert source!=original and "'/dll'" in source
    env=dict(base.__dict__,export_symbols=exports,export_definition=definition)
    exec(compile(source,str(HERE/'build.py'),'exec'),env);base.link_coff=env['link_coff']
    pe=helpers.aligned(base,base.pack_elf(elf),'D3DM.dll',table)
    aligned=validate_tables(pe)
    (out/'D3DM.dll').write_bytes(pe)
    scene_base=helpers.load_base(False)
    original_scene=inspect.getsource(scene_base.build)
    needle="'-I'+str(HERE), '-c', str(path)"
    assert original_scene.count(needle)==1
    scene_source=original_scene.replace(needle,"'-I'+str(sdk_include), "+needle)
    env=dict(scene_base.__dict__,sdk_include=out/'include')
    exec(compile(scene_source,str(HERE/'build.py'),'exec'),env)
    scene_report=env['build'](out/'scene',HERE/'tests/scenes.c')
    report=dict(abi,diagnostic=diagnostic,sha256=hashlib.sha256(pe).hexdigest(),bytes=len(pe),
                exports=list(table.values()),aligned_tables=aligned,
                imports={'coredll.dll':base.ORDINALS},
                scene=scene_report,
                sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (out/'BUILD.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--abi-only', action='store_true')
    p.add_argument('--no-diagnostics', action='store_true')
    a = p.parse_args()
    print(json.dumps(check_abi(a.out) if a.abi_only else build_adapter(a.out,not a.no_diagnostics)))
