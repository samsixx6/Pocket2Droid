"""Compile actual sound classes; host tests keep Android output as the boundary."""
from pathlib import Path
import argparse, subprocess, zipfile
HERE=Path(__file__).resolve().parents[1];ROOT=HERE.parent;SRC=HERE/'src/com/samsixx/pocket2droid'
p=argparse.ArgumentParser();p.add_argument('--protocol',action='store_true');p.add_argument('--pump',action='store_true');p.add_argument('--session',action='store_true');p.add_argument('--build-art',action='store_true');a=p.parse_args()
build=HERE/'build/audio-tests';build.mkdir(parents=True,exist_ok=True)
if a.protocol:
    assert (SRC/'AudioProtocol.java').exists(), 'Missing audio consumer protocol'
    producer=build/'producer'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'qemu/include'),str(ROOT/'tests/audio-output-test.c'),str(ROOT/'qemu/hw/dma/p2d-audio.c'),'-o',str(producer)],check=True)
    subprocess.run(['javac','--release','8','-d',str(build),str(SRC/'AudioProtocol.java'),str(HERE/'tests/AudioProtocolTest.java')],check=True)
    subprocess.run(['java','-cp',str(build),'com.samsixx.pocket2droid.AudioProtocolTest',str(build/'interop.bin'),str(producer)],check=True)
if a.pump:
    assert (SRC/'AudioPump.java').exists(), 'Missing bounded playback pump'
    subprocess.run(['javac','--release','8','-d',str(build),*[str(SRC/n) for n in ('AudioProtocol.java','AudioPump.java','PcmResampler.java')],str(HERE/'tests/AudioPumpTest.java')],check=True)
    subprocess.run(['java','-cp',str(build),'com.samsixx.pocket2droid.AudioPumpTest'],check=True)
if a.session:
    assert (SRC/'AudioSession.java').exists(), 'Missing stale-callback session gate'
    subprocess.run(['javac','--release','8','-d',str(build),str(SRC/'AudioSession.java'),str(HERE/'tests/AudioSessionTest.java')],check=True)
    subprocess.run(['java','-cp',str(build),'com.samsixx.pocket2droid.AudioSessionTest'],check=True)
if a.build_art:
    api=Path('/opt/pocket2droid-android-tools/android-36/android.jar');sdk=api.parent.parent/'android-16'
    classes=build/'art-classes';classes.mkdir(exist_ok=True)
    sources=list(SRC.glob('*.java'))+[HERE/'tests'/n for n in ('EngineKeyTest.java','AudioProtocolTest.java','AudioPumpTest.java','AudioSessionTest.java','AudioPlayerArtTest.java','AudioShortSoundArtTest.java','InsetBackdropArtTest.java')]
    subprocess.run(['javac','--release','8','-cp',str(api),'-d',str(classes),*map(str,sources)],check=True)
    jar=build/'art-input.jar';subprocess.run(['jar','cf',str(jar),'-C',str(classes),'.'],check=True)
    dex=build/'art-dex';dex.mkdir(exist_ok=True)
    subprocess.run(['java','-cp',str(sdk/'lib/d8.jar'),'com.android.tools.r8.D8','--min-api','24','--lib',str(api),'--output',str(dex),str(jar)],check=True)
    with zipfile.ZipFile(build/'audio-art.jar','w') as z:z.write(dex/'classes.dex','classes.dex')
    print(build/'audio-art.jar')
