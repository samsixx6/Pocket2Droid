package com.samsixx.pocket2droid;
import java.io.*;import java.util.UUID;
/** Graphics setup eligibility follows the installation across BIN changes. */
final class RomDeviceState {
 private RomDeviceState(){}
 static String prepare(File files,RomProfile profile)throws IOException{
  String old=ProfileFiles.name(profile.settingsKey);if(old.matches("rom-[0-9a-f]{32}"))return old;
  ProfileFiles.plain(files);if(!files.isDirectory())throw new IOException("Private ROM settings folder unavailable");
  String key="rom-"+UUID.randomUUID().toString().replace("-","");File graphics=new File(files,"gles"),prior=new File(graphics,GlesSetup.sha(old.getBytes("UTF-8")));ProfileFiles.plain(graphics);ProfileFiles.plain(prior);
  if(prior.exists()){
   if(!prior.isDirectory())throw new IOException("Invalid existing graphics setup history");
   File next=new File(graphics,GlesSetup.sha(key.getBytes("UTF-8")));ProfileFiles.plain(next);if(!next.mkdir())throw new IOException("Cannot preserve graphics setup history");
   if(!StoragePublish.syncDirectory(graphics)){if(next.list()!=null&&next.list().length==0)next.delete();throw new IOException("Cannot sync graphics setup history");}
  }return key;
 }
}
