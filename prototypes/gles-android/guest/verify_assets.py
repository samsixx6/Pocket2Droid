"""Independently inspect final CE PE tables and source/asset manifest hashes."""
from pathlib import Path
import hashlib, json, struct, sys
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parents[1]/'gles-support'))
from inspect_inputs import pe
def verify():
    manifest=json.loads((HERE/'build/manifest.json').read_text())
    assert manifest['mode_file']=='mode.txt' and manifest['mode_max_bytes']==16
    assert manifest['mode_values']==['on','on gpu','on cpu','off']
    assert manifest['texture_units']=={'default':2,'on':2,'on gpu':2,'on cpu':4}
    reports={}
    for name,digest in manifest['files'].items():
        data=(HERE/'build'/name).read_bytes()
        assert hashlib.sha256(data).hexdigest()==digest
        parsed=pe(data);assert parsed['architecture']=='ARM' and parsed['subsystem']==9
        head=struct.unpack_from('<I',data,60)[0];opt=head+24
        flags=struct.unpack_from('<H',data,head+22)[0]
        assert bool(flags&0x2000)==name.endswith('.dll')
        assert [i['dll'].lower() for i in parsed['imports']]==['coredll.dll']
        assert struct.unpack_from('<I',data,opt+16)[0]%4==0
        sections=opt+struct.unpack_from('<H',data,head+20)[0]
        rows=[struct.unpack_from('<8sIIIIIIHHI',data,sections+i*40) for i in range(struct.unpack_from('<H',data,head+6)[0])]
        def rva(addr):
            for row in rows:
                if row[2]<=addr<row[2]+max(row[1],row[3]):return row[4]+addr-row[2]
            raise AssertionError(hex(addr))
        imp,size=struct.unpack_from('<II',data,opt+104);assert imp%4==0
        for i in range(size//20):
            d=struct.unpack_from('<IIIII',data,rva(imp)+i*20)
            assert d[0]%4==0 and d[4]%4==0
        if name.endswith('.dll'):
            assert b'Pocket2Droid native GLES bridge\0' in data
            assert b'PocketHLE native CPU software bridge' not in data
            profile='cl' if '_CL.' in name else 'cm'
            table=json.loads((HERE/f'data/libgles_{profile}-ordinals.json').read_text())['ordinals']
            actual={str(e['ordinal']):e['name'] for e in parsed['exports']}
            assert all(actual.get(o)==n for o,n in table.items()),'Official ABI changed'
            import build_bridge
            assert actual==build_bridge.TABLES[profile]
            assert all(int(e['rva'],16)%4==0 for e in parsed['exports'])
            exp=struct.unpack_from('<I',data,opt+96)[0];d=struct.unpack_from('<IIHHIIIIIII',data,rva(exp))
            assert d[8]%4==0 and d[9]%4==0 and d[10]%2==0
        reports[name]={'bytes':len(data),'sha256':digest,'exports':len(parsed['exports']),'machine':'ARM','subsystem':9}
    for name,digest in manifest['sources'].items():assert hashlib.sha256((HERE/name).read_bytes()).hexdigest()==digest,name
    (HERE/'build/verification.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports,indent=2))
if __name__=='__main__':verify()
