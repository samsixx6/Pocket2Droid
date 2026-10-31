package com.samsixx.pocket2droid;
import java.io.*;
final class RomProfile {
    final String filename,displayName,iconId,error,saveState,settingsKey;
    final File rom,storage;
    final RomSettings settings;
    final boolean glesBuiltIn;
    RomProfile(String name,String icon,File rom,File storage,RomSettings settings,String state,String error){
        this(name,icon,rom,storage,settings,state,error,name);
    }
    RomProfile(String name,String icon,File rom,File storage,RomSettings settings,String state,String error,String settingsKey){
        glesBuiltIn=builtInGles(rom);this.settingsKey=settingsKey;filename=name;iconId=icon;this.rom=rom;this.storage=storage;this.settings=settings;displayName=settings.displayName;saveState=state;this.error=error;
    }
    /** Read the bounded XIP file table used by our raw Device Emulator ROMs.
     * Called while profiles load on the repository worker; never scan the full
     * image or infer graphics support from a user-editable filename. */
    static boolean builtInGles(File rom){
        if(rom==null||!rom.isFile())return false;
        try(RandomAccessFile input=new RandomAccessFile(rom,"r")){
            long length=input.length();if(length<0x30048)return false;
            input.seek(0x30040);if(input.readInt()!=0x45434543)return false; // ECEC
            long toc=(Integer.reverseBytes(input.readInt())&0xffffffffL)-0x88000000L;
            if(toc<0||toc>length-84)return false;
            byte[] header=new byte[84];input.seek(toc);input.readFully(header);
            long modules=u32(header,16),files=u32(header,48);
            if(modules>4096||files>4096)return false;
            long table=toc+84+modules*32,bytes=files*28;
            if(table>length||bytes>length-table)return false;
            byte[] entries=new byte[(int)bytes];input.seek(table);input.readFully(entries);
            for(int at=0;at<entries.length;at+=28){
                long name=u32(entries,at+20)-0x88000000L;
                if(name<0||name>=length)continue;
                byte[] text=new byte[(int)Math.min(64,length-name)];input.seek(name);input.readFully(text);
                int end=0;while(end<text.length&&text[end]!=0)end++;
                if(end==text.length)continue;
                String dll=new String(text,0,end,"US-ASCII");
                if(!dll.equalsIgnoreCase("libGLES_CL.dll")&&!dll.equalsIgnoreCase("libGLES_CM.dll")&&!dll.equalsIgnoreCase("libGLESv2.dll"))continue;
                long size=u32(entries,at+12),stored=u32(entries,at+16),data=u32(entries,at+24)-0x88000000L;
                if(size!=stored||stored<160||data<0||data>length||stored>length-data)continue;
                input.seek(data);if(input.readUnsignedShort()!=0x4d5a)continue;
                input.seek(data+60);long pe=(Integer.reverseBytes(input.readInt())&0xffffffffL);
                if(pe>stored-96)continue;
                byte[] image=new byte[96];input.seek(data+pe);input.readFully(image);
                if(u32(image,0)==0x4550&&u16(image,4)==0x1c0&&(u16(image,22)&0x2000)!=0&&u16(image,24)==0x10b&&u16(image,92)==9)return true;
            }
        }catch(IOException|SecurityException unavailable){return false;}
        return false;
    }
    private static int u16(byte[] data,int at){return(data[at]&255)|((data[at+1]&255)<<8);}
    private static long u32(byte[] data,int at){return(long)u16(data,at)|((long)u16(data,at+2)<<16);}
}
