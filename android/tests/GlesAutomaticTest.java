package com.samsixx.pocket2droid;
import java.io.*;import java.util.*;import org.json.*;
public final class GlesAutomaticTest {
 static int checks;static void yes(boolean value,String why){if(!value)throw new AssertionError(why);checks++;}
 static void profiles()throws Exception {
  RomSettings defaults=RomSettings.defaults("Original",new File("/tmp/card"));
  yes(defaults.glesEnabled&&defaults.glesGpuEnabled,"New ROM must use automatic GLES/GPU");
  for(int enabled=-1;enabled<=1;enabled++)for(int gpu=-1;gpu<=1;gpu++){
   JSONObject old=defaults.withCustomResolution(240,400).withInternetAccess(false).json();
   if(enabled<0)old.remove("glesEnabled");else old.put("glesEnabled",enabled==1);
   if(gpu<0)old.remove("glesGpuEnabled");else old.put("glesGpuEnabled",gpu==1);
   RomSettings read=RomSettings.parse(old);yes(read.glesEnabled&&read.glesGpuEnabled,"Legacy Off/CPU/missing choices must normalize to automatic GPU");
   JSONObject normalized=read.json();yes(normalized.getBoolean("glesEnabled")&&normalized.getBoolean("glesGpuEnabled"),"Saved profile must retain automatic policy");
   old.remove("glesEnabled");old.remove("glesGpuEnabled");normalized.remove("glesEnabled");normalized.remove("glesGpuEnabled");yes(old.similar(normalized),"Unrelated profile fields must be preserved");
   RomSettings renamed=read.withDisplayName("Renamed").withCard(new File("/tmp/other")).withOrientation(RomSettings.HostOrientation.LANDSCAPE);
   yes(renamed.glesEnabled&&renamed.glesGpuEnabled&&renamed.networkMac.equals(read.networkMac)&&renamed.customWidth==240&&renamed.customHeight==400,"Later profile edits must preserve policy and unrelated identity");
  }
  for(String key:new String[]{"glesEnabled","glesGpuEnabled"})for(Object bad:new Object[]{1,"false",JSONObject.NULL}){
   try{RomSettings.parse(defaults.json().put(key,bad));throw new AssertionError("Malformed profile flag accepted");}catch(IOException expected){checks++;}
  }
 }
 static void environment()throws Exception {
  ProcessBuilder builder=new ProcessBuilder("not-started");Map<String,String> env=builder.environment();env.clear();env.put("KEEP","unchanged");
  RomSettings normal=RomSettings.defaults("ROM",new File("/tmp/card"));RomSettings old=RomSettings.parse(normal.json().put("glesEnabled",false).put("glesGpuEnabled",false));
  for(RomSettings settings:new RomSettings[]{null,normal,old})for(String inherited:new String[]{"cpu","gpu","unrecognized"}){
   env.put("P2D_GLES_BACKEND",inherited);Engine.configureGlesBackend(builder,settings);yes("gpu".equals(env.get("P2D_GLES_BACKEND")),"Every launch must request GPU preferred, including legacy profiles");yes(env.size()==2&&"unchanged".equals(env.get("KEEP")),"Other environment entries must be untouched");
  }
 }
 public static void main(String[]args)throws Exception {if(args.length==0||args[0].equals("profiles"))profiles();if(args.length==0||args[0].equals("environment"))environment();System.out.println("PASS "+checks+" automatic GLES profile/launch checks");}
}
