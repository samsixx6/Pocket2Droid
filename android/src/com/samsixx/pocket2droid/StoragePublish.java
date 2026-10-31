package com.samsixx.pocket2droid;
import java.io.*;
final class StoragePublish {
    static { System.loadLibrary("p2dstorage"); }
    private static native boolean publishNoReplace(String from, String to) throws IOException;
    private static native boolean copyNoReplace(String from, String to) throws IOException;
    private static native boolean syncDirectoryPath(String directory) throws IOException;
    static void publish(File from, File to) throws IOException {
        publish(from,to,null);
    }
    private static final String PREFIX=".storage-publication-", SUFFIX=".incomplete";
    private static final int MAGIC=0x50324450;
    static synchronized void publish(File from, File to, File privateDirectory) throws IOException {
        if(publishNoReplace(from.getAbsolutePath(),to.getAbsolutePath())) return;
        if(privateDirectory==null || !privateDirectory.isDirectory())
            throw new IOException("This filesystem requires a private storage recovery record");
        long size=from.length(); String digest=StorageLayout.digest(from);
        File record=File.createTempFile(PREFIX,SUFFIX,privateDirectory);
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(record))) {
            out.writeInt(MAGIC);out.writeUTF(from.getCanonicalPath());out.writeUTF(to.getCanonicalPath());
            out.writeLong(size);out.writeUTF(digest);out.flush();
        }
        syncFile(record); requirePrivateSync(privateDirectory);
        if(!copyNoReplace(from.getAbsolutePath(),to.getAbsolutePath())) {
            retire(record,privateDirectory);
            throw new IOException("Existing "+to.getName()+" preserved; publication refused");
        }
        if(to.length()!=size || !StorageLayout.digest(to).equals(digest) || !StorageLayout.digest(from).equals(digest))
            throw new IOException("Storage publication verification failed; prepared input and recovery record preserved");
        requirePublicationSync(to.getParentFile());retire(record,privateDirectory);
    }
    private static void syncFile(File file) throws IOException {
        try(FileInputStream in=new FileInputStream(file)) { in.getFD().sync(); }
    }
    private static void requirePrivateSync(File directory) throws IOException {
        if(!syncDirectory(directory)) throw new IOException("Cannot durably record storage publication; Start blocked");
    }
    private static void requirePublicationSync(File directory) throws IOException {
        if(!syncDirectory(directory)) throw new IOException("Cannot confirm durable storage publication. Prepared input and recovery record preserved; Start blocked for recovery.");
    }
    private static void retire(File record, File directory) throws IOException {
        if(!record.delete()) throw new IOException("Cannot complete storage recovery record; Start blocked");
        requirePrivateSync(directory);
    }
    private static File[] records(File directory) throws IOException {
        File[] files=directory.listFiles((dir,name)->name.startsWith(PREFIX)&&name.endsWith(SUFFIX));
        if(files==null) throw new IOException("Cannot check storage recovery records; Start blocked");
        return files;
    }
    static synchronized void recover(File privateDirectory, File sharedRoot) throws IOException {
        String shared=sharedRoot.getCanonicalPath()+File.separator;
        File checkpoint=new File(privateDirectory,"external-layout-v1.complete").getCanonicalFile();
        for(File record:records(privateDirectory)) {
            File expectedRecord=new File(privateDirectory.getCanonicalFile(),record.getName());
            if(!record.getCanonicalFile().equals(expectedRecord) || !record.isFile() || record.length()>32768)
                throw new IOException("Invalid storage recovery record; retained files preserved");
            File source,target;long size;String digest;
            try(DataInputStream in=new DataInputStream(new FileInputStream(record))) {
                if(in.readInt()!=MAGIC) throw new IOException("Invalid storage recovery record; retained files preserved");
                source=new File(in.readUTF());target=new File(in.readUTF());size=in.readLong();digest=in.readUTF();
                if(in.read()!=-1) throw new IOException("Invalid storage recovery record; retained files preserved");
            }
            if(!target.isAbsolute() || !target.getCanonicalFile().equals(target) || !source.isAbsolute()
                || !source.getCanonicalFile().equals(source) || !source.getParentFile().equals(target.getParentFile())
                || !source.getName().startsWith(target.getName()+".") || !source.getName().endsWith(".pending")
                || !(target.getPath().startsWith(shared)||target.equals(checkpoint)) || size<0 || !digest.matches("[0-9a-f]{64}"))
                throw new IOException("Invalid storage recovery paths; retained files preserved");
            if(!target.isFile() || target.length()!=size || !StorageLayout.digest(target).equals(digest))
                throw new IOException("Interrupted publication of "+target.getName()+". Prepared input and destination preserved; Start blocked for recovery.");
            syncFile(target);requirePublicationSync(target.getParentFile());retire(record,privateDirectory);
            // Retained prepared inputs are not deleted by recovery: shared paths
            // may have been replaced externally since the interruption.
        }
    }
    static synchronized void cleanupPending(File pending, File privateDirectory) {
        try { if(records(privateDirectory).length==0 && pending.exists()) pending.delete(); }
        catch(IOException unavailable) { /* Preserve pending input if recovery cannot be checked. */ }
    }
    static void copyNewFile(File from,File to)throws IOException {
        if(!copyNoReplace(from.getAbsolutePath(),to.getAbsolutePath()))throw new IOException("Existing update destination preserved: "+to.getName());
    }
    static boolean syncDirectory(File directory) throws IOException {
        return syncDirectoryPath(directory.getAbsolutePath());
    }
}
