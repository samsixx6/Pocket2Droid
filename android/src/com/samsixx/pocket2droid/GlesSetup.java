package com.samsixx.pocket2droid;
import android.content.Context;
import java.io.*;
import java.security.*;
import java.util.*;
import org.json.*;

/** Verified private guest assets. No Android-side writes to ROM/card/game data. */
final class GlesSetup {
    static final String MARKER="P2D_GLES_ANDROID_V1";
    static final String[] ASSETS={"autorun.exe","setup.exe","libGLES_CL.dll","libGLES_CM.dll"};
    static final class Bundle {
        final File directory,launcher;final boolean enabled;
        Bundle(File directory,boolean enabled){this.directory=directory;launcher=new File(directory,"autorun.exe");this.enabled=enabled;}
    }
    private GlesSetup(){}
    static Bundle prepare(Context context,String filename,RomSettings settings,File network)throws IOException {
        File parent=new File(new File(context.getFilesDir().getCanonicalFile(),"gles"),sha(filename.getBytes("UTF-8")));
        Map<String,byte[]> files=new LinkedHashMap<>();
        for(String name:ASSETS)try(InputStream in=context.getAssets().open("gles-setup/"+name)){files.put(name,read(in,512*1024));}
        byte[] manifest;try(InputStream in=context.getAssets().open("gles-setup/manifest.json")){manifest=read(in,128*1024);}
        byte[] networkBytes=null;if(network!=null)try(InputStream in=new FileInputStream(network)){networkBytes=read(in,256*1024);}
        return publish(parent,files,manifest,true,true,networkBytes);
    }
    static String appendMachineOptions(String machine,Bundle setup,File renderer){
        if(setup==null)return machine;
        machine+=",gles-assets="+option(setup.directory)+",network-setup="+option(setup.launcher);
        if(setup.enabled)machine+=",gles-renderer="+option(renderer);
        return machine;
    }
    private static String option(File f){return f.getPath().replace(",",",,");}
    static synchronized Bundle publish(File parent,Map<String,byte[]> files,byte[] manifest,boolean enabled,byte[] network)throws IOException {
        return publish(parent,files,manifest,enabled,true,network);
    }
    static synchronized Bundle publish(File parent,Map<String,byte[]> files,byte[] manifest,boolean enabled,boolean gpu,byte[] network)throws IOException {
        if(!files.keySet().equals(new LinkedHashSet<>(Arrays.asList(ASSETS))))throw new IOException("Unexpected GLES asset inventory");
        JSONObject hashes;
        try{hashes=new JSONObject(new String(manifest,"UTF-8")).getJSONObject("files");
            for(String name:ASSETS){String expected=hashes.getString(name);if(!expected.matches("[0-9a-f]{64}")||!expected.equals(sha(files.get(name))))throw new IOException("GLES asset identity mismatch: "+name);}
        }catch(JSONException bad){throw new IOException("Invalid GLES asset manifest",bad);}
        for(String name:ASSETS)validatePe(files.get(name),name.endsWith(".dll"));
        if(network!=null)validatePe(network,false);
        File canonical=parent.getAbsoluteFile();if(!canonical.equals(canonical.getCanonicalFile()))throw new IOException("GLES private directory cannot be a link");
        if(!canonical.isDirectory()&&!canonical.mkdirs()&&!canonical.isDirectory())throw new IOException("Cannot prepare private GLES directory");
        byte[] mode=(enabled?(gpu?"on gpu":"on cpu"):"off").getBytes("UTF-8");
        String identity=sha(concat(manifest,mode,network==null?new byte[0]:network));
        File directory=new File(canonical,identity);if(!directory.equals(directory.getCanonicalFile()))throw new IOException("GLES cache cannot be a link");
        if(!directory.isDirectory()&&!directory.mkdir()&&!directory.isDirectory())throw new IOException("Cannot prepare GLES assets");
        File marker=new File(directory,".p2d-gles-owned");
        if(marker.exists()){try(InputStream in=new FileInputStream(marker)){if(!new String(read(in,128),"UTF-8").equals(MARKER))throw new IOException("Unexpected GLES cache ownership");}}
        else publishFile(marker,MARKER.getBytes("UTF-8"));
        for(String name:ASSETS)publishFile(new File(directory,name),files.get(name));
        publishFile(new File(directory,"manifest.json"),manifest);
        publishFile(new File(directory,"mode.txt"),mode);
        if(network!=null)publishFile(new File(directory,"network.exe"),network);
        return new Bundle(directory,enabled);
    }
    private static byte[] concat(byte[] a,byte[] b,byte[] c)throws IOException{ByteArrayOutputStream out=new ByteArrayOutputStream();out.write(a);out.write(b);out.write(c);return out.toByteArray();}
    private static void publishFile(File target,byte[] bytes)throws IOException {
        if(!target.getAbsoluteFile().equals(target.getCanonicalFile()))throw new IOException("GLES cache entry cannot be a link");
        if(target.exists()){if(!target.isFile())throw new IOException("Invalid GLES cache entry");try(InputStream in=new FileInputStream(target)){if(sha(read(in,1024*1024)).equals(sha(bytes)))return;}throw new IOException("GLES cache entry changed; preserved: "+target.getName());}
        File temp=File.createTempFile(".gles-",".tmp",target.getParentFile());
        try{try(FileOutputStream out=new FileOutputStream(temp)){out.write(bytes);out.getFD().sync();}if(!temp.setReadOnly()||!temp.renameTo(target))throw new IOException("Cannot publish GLES asset");}finally{temp.delete();}
    }
    static byte[] read(InputStream in,int maximum)throws IOException{ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int n;while((n=in.read(buffer))!=-1){if(n==0)continue;if(n>maximum-out.size())throw new IOException("GLES asset exceeds size limit");out.write(buffer,0,n);}return out.toByteArray();}
    static String sha(byte[] data)throws IOException{try{StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(data))s.append(String.format(Locale.ROOT,"%02x",b&255));return s.toString();}catch(NoSuchAlgorithmException bad){throw new IOException(bad);}}
    static void validatePe(byte[] b,boolean dll)throws IOException{
        if(b.length<512||b.length>512*1024||b[0]!='M'||b[1]!='Z')throw new IOException("Invalid GLES CE image");
        long offset=u32(b,60);if(offset>b.length-96)throw new IOException("Invalid GLES PE header");int p=(int)offset;
        if(b[p]!='P'||b[p+1]!='E'||b[p+2]!=0||b[p+3]!=0||u16(b,p+4)!=0x1c0||u16(b,p+20)<72||u16(b,p+24)!=0x10b||u16(b,p+92)!=9||((u16(b,p+22)&0x2000)!=0)!=dll)throw new IOException("GLES asset must be an ARM CE image");
    }
    private static int u16(byte[] b,int n){return(b[n]&255)|((b[n+1]&255)<<8);}
    private static long u32(byte[] b,int n){return(long)u16(b,n)|((long)u16(b,n+2)<<16);}
}
