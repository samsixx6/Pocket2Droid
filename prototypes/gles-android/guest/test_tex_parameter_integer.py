"""Execute the real named ARM entry point and preserve every previous export."""
from pathlib import Path
import argparse, importlib.util, json, struct, sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'gles-support'))
from inspect_inputs import pe as inspect_pe

p = argparse.ArgumentParser()
p.add_argument('--dll-dir', type=Path, default=HERE / 'build')
p.add_argument('--baseline', type=Path)
p.add_argument('--execute', action='store_true')
a = p.parse_args()
def stub_bytes(data, rva):
    head = struct.unpack_from('<I', data, 60)[0]
    first = head + 24 + struct.unpack_from('<H', data, head + 20)[0]
    for i in range(struct.unpack_from('<H', data, head + 6)[0]):
        sec = struct.unpack_from('<8sIIII', data, first + i * 40)
        if sec[2] <= rva < sec[2] + max(sec[1], sec[3]):
            offset = sec[4] + rva - sec[2]
            return data[offset:offset + 40]
    raise AssertionError('Export RVA outside PE sections')
rows = []
for profile in ('CL', 'CM'):
    name = 'libGLES_' + profile + '.dll'
    data = (a.dll_dir / name).read_bytes()
    exports = {e['name']: e for e in inspect_pe(data)['exports']}
    assert 'glTexParameteri' in exports, name + ': missing glTexParameteri named export'
    assert exports['glTexParameteri']['ordinal'] == 153
    if a.baseline:
        old_data = (a.baseline / name).read_bytes()
        prior = inspect_pe(old_data)['exports']
        for old in prior:
            assert exports[old['name']]['ordinal'] == old['ordinal'], 'Existing ordinal changed: ' + old['name']
            assert stub_bytes(data, int(exports[old['name']]['rva'], 16)) == stub_bytes(old_data, int(old['rva'], 16)), 'Existing ARM stub changed: ' + old['name']
    entry = int(exports['glTexParameteri']['rva'], 16)
    header = struct.unpack_from('<I', data, 60)[0]
    opt = header + 24
    image_base = struct.unpack_from('<I', data, opt + 28)[0]
    first = opt + struct.unpack_from('<H', data, header + 20)[0]
    sections = [struct.unpack_from('<8sIIII', data, first + i * 40) for i in range(struct.unpack_from('<H', data, header + 6)[0])]
    if a.execute:
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
        for parameter in (0x2600, 0x2601, 0x812f, 0xffffffff, 0x12345678):
            uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
            uc.mem_map(image_base, 0x200000)
            for sec in sections:
                if sec[3]:
                    uc.mem_write(image_base + sec[2], data[sec[4]:sec[4] + sec[3]])
            stack, sentinel = image_base + 0x1f0000, image_base + 0x1ff000
            uc.reg_write(UC_ARM_REG_SP, stack)
            uc.reg_write(UC_ARM_REG_LR, sentinel)
            uc.reg_write(UC_ARM_REG_R0, 0x0de1)
            uc.reg_write(UC_ARM_REG_R1, 0x2801)
            uc.reg_write(UC_ARM_REG_R2, parameter)
            uc.reg_write(UC_ARM_REG_R3, 0xfeedbeef)
            uc.reg_write(UC_ARM_REG_R4, 0xabcdef12)
            observed = []
            def hook(uc, address, size, opaque):
                opcode = struct.unpack('<I', uc.mem_read(address, 4))[0]
                if opcode & 0xff000000 == 0xeb000000:
                    observed.append((uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R3), struct.unpack('<3I', uc.mem_read(uc.reg_read(UC_ARM_REG_R1), 12))))
                    assert observed[-1] == (135, 3, (0x0de1, 0x2801, parameter)), 'Integer enum must retain its exact bits'
                    uc.reg_write(UC_ARM_REG_R0, 0x76543210)
                    uc.reg_write(UC_ARM_REG_PC, address + 4)
            uc.hook_add(UC_HOOK_CODE, hook)
            uc.emu_start(image_base + entry, sentinel, count=30)
            assert len(observed) == 1
            assert uc.reg_read(UC_ARM_REG_SP) == stack and uc.reg_read(UC_ARM_REG_R4) == 0xabcdef12
            assert uc.reg_read(UC_ARM_REG_R0) == 0x76543210
    rows.append(dict(dll=name, named_export=True, ordinal=153, executed=a.execute, parameter_cases=5 if a.execute else 0, unchanged_existing_stubs=len(prior) if a.baseline else None))
print(json.dumps(dict(passed=True, checks=rows), indent=2))
