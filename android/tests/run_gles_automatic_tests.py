"""Actual frontend profile/launch regression; no APK, process or guest-data writes."""
from pathlib import Path
import argparse,subprocess
A=Path(__file__).resolve().parents[1];B=A/'build/gles-automatic';B.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--case',choices=['profiles','environment']);args=p.parse_args()
cp=str(A/'tests/dependencies/json-20240303.jar')+':/opt/pocket2droid-android-tools/android-36/android.jar'
subprocess.run(['javac','--release','8','-encoding','UTF-8','-cp',cp,'-d',str(B),*map(str,(A/'src').rglob('*.java')),str(A/'tests/GlesAutomaticTest.java'),str(A/'tests/RomGlesBadgeTest.java')],check=True)
subprocess.run(['java','-cp',str(B)+':'+cp,'com.samsixx.pocket2droid.GlesAutomaticTest',*([args.case] if args.case else [])],check=True)

if not args.case:subprocess.run(['java','-cp',str(B)+':'+cp,'com.samsixx.pocket2droid.RomGlesBadgeTest'],check=True)
