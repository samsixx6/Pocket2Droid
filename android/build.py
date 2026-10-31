"""Incremental Android frontend package; exact original key, no ROM/saves inside."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]
LEGACY = PROJECT / 'SOURCE/review-v640/Pocket2Droid-V6.40-Clean-Source'
TOOLS = Path('/opt/pocket2droid-android-tools')
SDK = TOOLS / 'android-16'
ANDROID = TOOLS / 'android-36/android.jar'
BASELINE = PROJECT / 'apk/6.52/Pocket2Droid-V6.52-Persistent-JIT-DEV-SIGNED.apk'
BASELINE_SHA = 'f491e563c1db6a62d3be8c952f56ee4556ff39e8138c9bc5b1c92de9de1fd058'
VERSION = '1.0.3.1'
VERSION_CODE = 703

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m

def manifest_text(test_package=False):
    text = (HERE / 'AndroidManifest.xml').read_text()
    if test_package:
        text = text.replace('package="com.samsixx.pocket2droid"', 'package="com.samsixx.p2dtest"')
        # Fully qualified source activities also serve the disposable smoke package.
        text = text.replace('<application ', '<application android:debuggable="true" ', 1)
        text = text.replace('android:label="Pocket2Droid"', 'android:label="P2D disposable frontend test"')
        text = re.sub(r'\s*<activity android:name="android.app.NativeActivity".*?</activity>', '', text, flags=re.S)
    return text

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-package', action='store_true', help='Disposable debuggable smoke package; never a user release')
    args = parser.parse_args()
    package = 'com.samsixx.p2dtest' if args.test_package else 'com.samsixx.pocket2droid'
    variant = 'smoke' if args.test_package else 'candidate'
    build = HERE / 'build'; build.mkdir(exist_ok=True)
    started = time.perf_counter()
    cache = module('p2d_build_cache', LEGACY / 'build_workflow.py').Cache(HERE)
    setup_builder = module('p2d_guest_network_setup', HERE.parent / 'guest/network-setup/build.py')
    setup_dir = build / 'network-setup/production-helper'
    setup_record = setup_builder.build(setup_dir)
    if setup_record['builder_sha256'] != sha(HERE.parent / 'guest/network-setup/build.py'):
        raise ValueError('Guest network helper recipe is stale')
    for path, expected in setup_record['sources'].items():
        if sha(path) != expected: raise ValueError('Guest network helper source is stale: ' + path)
    setup_assets = [setup_dir / name for name in ('autorun.exe', 'manifest.json')]
    if sha(setup_assets[0]) != setup_record['executable_sha256']:
        raise ValueError('Guest network helper executable differs from manifest')
    identity = json.dumps({'jdk': subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT, text=True),
                           'javac': sha(Path(shutil.which('javac')).resolve()),
                           'android': sha(ANDROID), 'd8': sha(SDK / 'lib/d8.jar')}, sort_keys=True)
    sources = sorted((HERE / 'src').rglob('*.java'))
    classes = build / 'classes'; jar = build / 'classes.jar'
    def compile_java():
        if classes.exists():
            if not classes.resolve().is_relative_to(build.resolve()): raise ValueError('Class output path escape')
            shutil.rmtree(classes)
        classes.mkdir()
        subprocess.run(['javac', '--release', '8', '-encoding', 'UTF-8', '-cp', str(ANDROID), '-d', str(classes),
                        *map(str, sources)], check=True)
        subprocess.run(['jar', 'cf', str(jar), '-C', str(classes), '.'], check=True)
    cache.run('Java classes', ['javac', '--release=8'], [*sources, Path(__file__)], [jar], identity, compile_java)
    dex = build / 'dex'; dex.mkdir(exist_ok=True)
    cache.run('dex', ['java', '-cp', str(SDK / 'lib/d8.jar'), 'com.android.tools.r8.D8', '--min-api', '24',
                     '--lib', str(ANDROID), '--output', str(dex), str(jar)],
              [jar, SDK / 'lib/d8.jar', ANDROID, Path(__file__)], [dex / 'classes.dex'], identity)
    res = build / 'res/drawable'; res.mkdir(parents=True, exist_ok=True)
    icon = HERE / 'res/drawable/pocket2droid_icon.png'
    target_icon = res / 'pocket2droid_icon.png'
    cache.run('icon', ['copy'], [icon], [target_icon], action=lambda: shutil.copyfile(icon, target_icon))
    extra_resources = sorted(p for p in (HERE / 'res').rglob('*') if p.is_file())
    wanted = {p.relative_to(HERE / 'res') for p in extra_resources}
    for stale in sorted(p for p in res.parent.rglob('*') if p.is_file()):
        if stale.relative_to(res.parent) not in wanted:
            if not stale.resolve().is_relative_to(res.parent.resolve()): raise ValueError('Resource cache path escape')
            stale.unlink()
    for source in extra_resources:
        target = res.parent / source.relative_to(HERE / 'res')
        target.parent.mkdir(parents=True, exist_ok=True)
        cache.run('resource ' + source.name, ['copy'], [source], [target], action=lambda s=source,t=target: shutil.copyfile(s,t))
    resource_inputs = sorted(p for p in res.parent.rglob('*') if p.is_file())
    compiled_res = build / 'resources.zip'
    cache.run('compile resources', [str(SDK / 'aapt2'), 'compile', '--dir', str(res.parent), '-o', str(compiled_res)],
              [*resource_inputs, SDK / 'aapt2', Path(__file__)], [compiled_res], identity)
    manifest = build / (variant + '-manifest.xml')
    text = manifest_text(args.test_package)
    if not manifest.exists() or manifest.read_text() != text: manifest.write_text(text)
    linked = build / (variant + '-resources.apk')
    cache.run('link resources ' + variant, [str(SDK / 'aapt2'), 'link', '-I', str(ANDROID), '--manifest', str(manifest),
                                          '-o', str(linked), str(compiled_res)],
              [manifest, compiled_res, SDK / 'aapt2', ANDROID], [linked], identity)
    if sha(BASELINE) != BASELINE_SHA: raise ValueError('Protected V6.52 APK identity mismatch')
    native = json.loads((build / 'bionic-engine.json').read_text())
    runtime = json.loads((build / 'runtime.json').read_text())
    if native['binary_sha256'] != sha(build / 'libp2dengine-bionic.so'): raise ValueError('Native engine binary differs from build record')
    if native['builder_sha256'] != sha(HERE / 'build_bionic.py'): raise ValueError('Android native recipe changed; rebuild required')
    patch_text = (build / 'termux-reference/0016-Add-workaround-for-tagged-pointers-on-Android-12.patch').read_text().replace('#ifdef __TERMUX__', '#ifdef __ANDROID__')
    if hashlib.sha256(patch_text.encode()).hexdigest() != native['termux_patch_sha256']: raise ValueError('Android heap-tag compatibility patch changed')
    for name, expected in native['support_sources'].items():
        if sha(build / 'termux-reference/setjmp-aarch64' / name) != expected: raise ValueError('Android setjmp source changed')
    if runtime['native_engine_record_sha256'] != sha(build / 'bionic-engine.json') or runtime['packager_sha256'] != sha(HERE / 'package_runtime.py') or runtime['tools_record_sha256'] != sha(build / 'bionic-tools.json'):
        raise ValueError('Android runtime packaging inputs changed')
    libraries = [build / 'runtime' / name for name in runtime['files']]
    for library in libraries:
        if sha(library) != runtime['files'][library.name]['sha256']: raise ValueError('Runtime library identity mismatch: ' + library.name)
    storage_runtime = json.loads((build / 'storage-runtime.json').read_text())
    storage_library = build / 'storage-runtime/libp2dstorage.so'
    if sha(storage_library) != storage_runtime['binary_sha256']:
        raise ValueError('Storage helper binary differs from build record')
    if sha(HERE / 'build_storage.py') != storage_runtime['builder_sha256']:
        raise ValueError('Storage helper recipe changed; rebuild required')
    for name, expected in storage_runtime['sources'].items():
        source = (HERE / name).resolve(strict=True)
        if not source.is_relative_to(HERE.resolve()) or sha(source) != expected:
            raise ValueError('Stale storage helper source: ' + name)
    libraries.append(storage_library)
    engine = build / 'runtime/libp2dengine.so'
    slirp_license=build/'networking/android/source/COPYRIGHT'
    slirp_notice=HERE/'patches/LIBSLIRP-NOTICE.txt'
    slirp_record=json.loads((build/'networking/android/provenance.json').read_text())
    if native.get('slirp')!=slirp_record or sha(slirp_license)!=slirp_record['source_sha256']['COPYRIGHT']:
        raise ValueError('Static libslirp provenance/notices differ from verified native build')
    sync = module('p2d_source_sync', HERE.parent / 'sync_source.py')
    if sync.git_names('diff', '--name-only', '-z', '--diff-filter=D', sync.BASE): raise ValueError('Native source deletion requires explicit rebuild handling')
    paths = set(sync.git_names('diff', '--name-only', '-z', '--diff-filter=ACMR', sync.BASE))
    paths.update(sync.git_names('ls-files', '--others', '--exclude-standard', '-z'))
    if native['source']['base'] != sync.BASE or paths != set(native['source']['paths']):
        raise ValueError('Native source inventory changed; run build_engine.py before packaging')
    for name, expected in native['source']['paths'].items():
        if sha(HERE.parent / 'qemu' / name) != expected: raise ValueError('Stale native source: ' + name)
    unsigned = build / ('Pocket2Droid-V' + VERSION + '-TCG-' + variant.upper() + '-UNSIGNED.apk')
    def package_apk():
        entries = []
        with zipfile.ZipFile(linked) as z:
            entries.extend((name, z.read(name), 4) for name in z.namelist())
        entries += [('classes.dex', (dex / 'classes.dex').read_bytes(), 4),
                    ('assets/QEMU-COPYING.txt', (HERE.parent / 'qemu/COPYING').read_bytes(), 4)]
        entries += [('assets/LIBSLIRP-COPYRIGHT.txt',slirp_license.read_bytes(),4),
                    ('assets/LIBSLIRP-NOTICE.txt',slirp_notice.read_bytes(),4)]
        entries += [('assets/network-setup/'+asset.name,asset.read_bytes(),4) for asset in setup_assets]
        entries.extend(('lib/arm64-v8a/' + library.name, library.read_bytes(), 16384) for library in libraries)
        with zipfile.ZipFile(unsigned, 'w', compression=zipfile.ZIP_STORED, allowZip64=False) as z:
            for name, content, align in entries:
                info = zipfile.ZipInfo(name, date_time=(2026, 10, 8, 12, 0, 0)); info.external_attr = 0o100755 << 16
                start = z.fp.tell() + 30 + len(name.encode())
                # Official zipalign supplies the alignment metadata preserved by
                # apksigner when it adds v1/v2/v3 signatures.
                z.writestr(info, content)
    cache.run('package ' + variant, ['package', variant],
              [linked, dex / 'classes.dex', *libraries, *setup_assets, build / 'runtime.json', build / 'storage-runtime.json', HERE.parent / 'qemu/COPYING',slirp_license,slirp_notice,Path(__file__)],
              [unsigned], identity, package_apk)
    aligned = unsigned.with_name(unsigned.stem + '-ALIGNED.apk')
    cache.run('align ' + variant, [str(SDK / 'zipalign'), '-f', '-P', '16', '4', str(unsigned), str(aligned)],
              [unsigned, SDK / 'zipalign', Path(__file__)], [aligned], identity)
    key = PROJECT / 'Key/wm6-v2-development.p12'
    notes = (key.parent / 'README.txt').read_text()
    match = re.search(r'(?im)\bpassword\s*[:=]\s*([^\r\n]+)', notes)
    if not match: raise ValueError('Existing signing configuration lacks a password field')
    password = match.group(1).strip().strip('`"\'')
    secret = TOOLS / '.p2d-signing-password'
    if secret.exists(): raise ValueError('Another signer may be running')
    secret.touch(mode=0o600, exist_ok=False)
    signed = build / ('Pocket2Droid-V' + VERSION + '-TCG-' + variant.upper() + '-SIGNED.apk')
    try:
        secret.write_text(password + '\n')
        subprocess.run(['java', '-jar', str(SDK / 'lib/apksigner.jar'), 'sign', '--ks', str(key), '--ks-type', 'PKCS12',
                        '--ks-pass', 'file:' + str(secret), '--out', str(signed), str(aligned)], check=True)
    finally:
        secret.unlink(missing_ok=True)
    verify = subprocess.check_output(['java', '-jar', str(SDK / 'lib/apksigner.jar'), 'verify', '--verbose',
                                      '--print-certs', str(signed)], text=True)
    if '85d6f87a557def48710b8e295ed58513888a99852843a90106b3b7f18e39681b' not in verify:
        raise ValueError('Signer identity mismatch')
    subprocess.run([str(SDK / 'zipalign'), '-c', '-P', '16', '4', str(signed)], check=True)
    (build / (variant + '-signature.txt')).write_text(verify)
    with zipfile.ZipFile(signed) as z:
        assert z.testzip() is None
        assert hashlib.sha256(z.read('lib/arm64-v8a/libp2dengine.so')).hexdigest() == sha(engine)
        for library in libraries:
            assert hashlib.sha256(z.read('lib/arm64-v8a/' + library.name)).hexdigest() == sha(library)
        assert 'lib/arm64-v8a/libwm6android.so' not in z.namelist()
        assert z.read('classes.dex').startswith(b'dex\n')
        manifest_data = z.read('AndroidManifest.xml')
    manifest_parser = module('p2d_manifest_verifier', LEGACY / 'verify_signed_apk.py')
    elements = manifest_parser.parse_manifest(manifest_data)
    by_name = {}
    for name, attrs in elements: by_name.setdefault(name, []).append(attrs)
    actual = by_name['manifest'][0]; app = by_name['application'][0]; sdk = by_name['uses-sdk'][0]
    assert actual['package'] == package and actual['versionCode'] == VERSION_CODE and actual['versionName'] == VERSION
    assert not any(attrs.get('name') == 'android.app.NativeActivity' for attrs in by_name.get('activity', []))
    assert sdk['minSdkVersion'] == 24 and sdk['targetSdkVersion'] == 36
    assert app['extractNativeLibs'] == 0xffffffff
    assert bool(app.get('debuggable', 0)) == args.test_package
    report = {'package': package, 'version': VERSION, 'test_only': args.test_package,
              'signed_apk': str(signed), 'apk_sha256': sha(signed), 'engine_sha256': sha(engine),
              'previous_engine_packaged': False, 'storage_helper_sha256': sha(storage_library), 'seconds': time.perf_counter() - started,
              'stages': cache.records}
    (build / (variant + '-build.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
