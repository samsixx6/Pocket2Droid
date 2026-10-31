package com.samsixx.pocket2droid;
import java.io.*;import java.lang.reflect.*;import java.nio.file.*;
/** Real bounded file parsing; synthetic ROM metadata and PE stubs only. */
public final class RomGlesBadgeTest {
 static int checks;static final long BASE=0x88000000L;static void yes(boolean b,String why){if(!b)throw new AssertionError(why);checks++;}
 static boolean detect(File f)throws Exception{try{return(Boolean)RomProfile.class.getDeclaredMethod("builtInGles",File.class).invoke(null,f);}catch(NoSuchMethodException missing){throw new AssertionError("Built-in GLES libraries must be detected from ROM content",missing);}}
 static void le(RandomAccessFile f,long n)throws Exception{f.writeInt(Integer.reverseBytes((int)n));}
 static File fixture(Path directory,String filename,String dll,boolean pe,boolean module)throws Exception{
  File file=directory.resolve(filename).toFile();try(RandomAccessFile f=new RandomAccessFile(file,"rw")){
   f.setLength(0x50000);f.seek(0x30040);f.writeBytes("ECEC");le(f,BASE+0x31000);f.seek(0x31010);le(f,module?1:0);f.seek(0x31030);le(f,module?0:1);
   f.seek(0x31000+84+(module?16:20));le(f,BASE+0x32000);
   if(!module){f.seek(0x31000+84+12);le(f,256);le(f,256);le(f,BASE+0x32000);le(f,BASE+0x33000);}
   f.seek(0x32000);f.writeBytes(dll);f.write(0);
   if(pe){f.seek(0x33000);f.writeBytes("MZ");f.seek(0x33000+60);le(f,64);f.seek(0x33000+64);f.write(new byte[]{80,69,0,0,(byte)0xc0,1});f.seek(0x33000+64+22);f.write(new byte[]{0,0x20});f.seek(0x33000+64+24);f.write(new byte[]{11,1});f.seek(0x33000+64+92);f.write(new byte[]{9,0});}
  }return file;
 }
 public static void main(String[]args)throws Exception{
  if(args.length>0){for(int i=0;i<args.length;i+=2)yes(detect(new File(args[i+1]))==Boolean.parseBoolean(args[i]),"Actual ROM capability mismatch: "+args[i+1]);System.out.println("PASS "+checks+" actual ROM badge checks");return;}
  Path directory=Files.createTempDirectory("p2d-gles-badge-");try{
   // FILESentry used by our bridge ROMs. A raw name in arbitrary bytes is insufficient.
   for(String name:new String[]{"libGLES_CL.dll","libGLES_CM.dll","libGLESv2.dll","LiBgLeS_Cm.DlL"})yes(detect(fixture(directory,"neutral-"+checks+".BIN",name,true,false)),"Uncompressed system GLES DLL must be recognized after rename");
   yes(!detect(fixture(directory,"GLES-ROM.BIN","coredll.dll",true,false)),"Filename must not claim GLES support");
   yes(!detect(fixture(directory,"fake.BIN","libGLES_CM.dll",false,false)),"Named junk must not claim a GLES bridge");
   yes(!detect(fixture(directory,"EGL-only.BIN","libEGL.dll",true,false)),"EGL name alone is not a GLES renderer");
   File file=fixture(directory,"reuse.BIN","libGLES_CM.dll",true,false);yes(detect(file),"Initial bridge should be detected");try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x33000);f.writeBytes("NO");}yes(!detect(file),"Same-path ROM replacement must not retain a stale badge");
   file=fixture(directory,"bounds.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x30044);le(f,0xffffffffL);}yes(!detect(file),"Out-of-file TOC must be rejected");
   file=fixture(directory,"count.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x31030);le(f,0xffffffffL);}yes(!detect(file),"Unbounded file count must be rejected");
   file=fixture(directory,"pointer.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x31000+84+20);le(f,BASE+file.length());}yes(!detect(file),"Out-of-file name must be rejected");
   file=fixture(directory,"compressed.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x31000+84+16);le(f,127);}yes(!detect(file),"Compressed or malformed stub must not be misread as a bridge file");
   file=fixture(directory,"machine.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.seek(0x33000+64+4);f.write(new byte[]{0x64,(byte)0x86});}yes(!detect(file),"Wrong machine must be rejected");
   file=fixture(directory,"short.BIN","libGLES_CM.dll",true,false);try(RandomAccessFile f=new RandomAccessFile(file,"rw")){f.setLength(32);}yes(!detect(file),"Truncated ROM must be harmless");yes(!detect(directory.resolve("missing.BIN").toFile()),"Missing ROM must be harmless");
  }finally{try(java.util.stream.Stream<Path> files=Files.list(directory)){files.forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}Files.delete(directory);}
  System.out.println("PASS "+checks+" built-in GLES badge file checks");
 }
}
