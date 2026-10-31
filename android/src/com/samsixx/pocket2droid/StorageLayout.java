package com.samsixx.pocket2droid;
import android.content.Context;
import android.os.Environment;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

final class StorageLayout {
    // The current emulated board has a 96MiB NOR region; ROM identity is unrestricted.
    static final long MAX_ROM_SIZE = 96L * 1024 * 1024, STORAGE_SIZE = 32L * 1024 * 1024;
    final File root, roms, sdcard, data, rom, cab, storage, log, runtime, frames, audio, control, temporary;
    private final File marker;
    final RomProfile profile;
    static File rootForPackage(String packageName, File sharedVolume, File appExternal) {
        if(sharedVolume==null) return null;
        File volume="com.samsixx.p2dtest".equals(packageName)?new File(sharedVolume,".p2d-disposable-frontend"):sharedVolume;
        return volume==null?null:new File(volume,"pocket2droid");
    }
    static StorageLayout forContext(Context context) {
        File shared=null;
        if(Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
            shared=rootForPackage(context.getPackageName(),Environment.getExternalStorageDirectory(),null);
        }
        return new StorageLayout(shared,new File(context.getFilesDir(),"tcg-v1"));
    }
    static StorageLayout forContext(Context context,RomProfile profile) {
        File shared=Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())?rootForPackage(context.getPackageName(),Environment.getExternalStorageDirectory(),null):null;
        return new StorageLayout(shared,new File(context.getFilesDir(),"tcg-v1"),profile);
    }
    StorageLayout(File shared, File legacy) {
        this(shared,legacy,null);
    }
    StorageLayout(File shared,File legacy,RomProfile selected) {
        root=shared; runtime=legacy;profile=selected;
        roms=root==null?null:new File(root,"Roms");sdcard=selected!=null?selected.settings.card:(root==null?null:new File(root,"sdcard"));data=root==null?null:new File(root,"data");
        rom=selected!=null?selected.rom:(roms==null?null:new File(roms,"PPC_USA.BIN"));cab=selected!=null||sdcard==null?null:new File(sdcard,"RagingThunder.cab");
        storage=selected!=null?selected.storage:(data==null?null:new File(data,"ram-fmd.bin"));log=selected!=null?new File(storage.getParentFile(),"engine.log"):(data==null?null:new File(data,"engine.log"));
        frames=new File(runtime,"frames.bin");control=new File(runtime,"control.sock");temporary=new File(runtime,"tmp");
        audio=new File(runtime,"audio.bin");
        marker=new File(runtime,"external-layout-v1.complete");
    }
    synchronized void prepare() throws IOException {
        if(root==null) throw new IOException("External storage unavailable. Reopen the app when shared storage is mounted; existing files preserved.");
        directory(root); directory(runtime);
        if (!root.getCanonicalFile().getName().equals("pocket2droid"))
            throw new IOException("Storage root is redirected; expected pocket2droid");
        if(profile!=null) {
            RomProfile current=new RomRepository(root,runtime,profile.settings.card).verifySessionProfile(profile.filename);
            if(!java.util.Objects.equals(current.settingsKey,profile.settingsKey)||!current.rom.equals(rom)||!current.storage.equals(storage)||!current.settings.card.equals(sdcard)||current.settings.scale!=profile.settings.scale||current.settings.orientation!=profile.settings.orientation||current.settings.screenPosition!=profile.settings.screenPosition||current.settings.resolution!=profile.settings.resolution||current.settings.customWidth!=profile.settings.customWidth||current.settings.customHeight!=profile.settings.customHeight||current.settings.internetEnabled!=profile.settings.internetEnabled||current.settings.networkSetupEligible!=profile.settings.networkSetupEligible||!java.util.Objects.equals(current.settings.networkMac,profile.settings.networkMac))
                throw new IOException("ROM profile changed before launch; return to the library and retry");
            unchangedPath(log);return;
        }
        for (File folder : new File[]{roms, sdcard, data}) {
            unchangedPath(folder); directory(folder);
        }
        StoragePublish.recover(runtime,root);
        unchangedPath(marker); unchangedPath(storage); unchangedPath(rom); unchangedPath(cab); unchangedPath(log);
        if (marker.exists()) {
            if (!marker.isFile() || marker.length() > 4096 || !readText(marker).equals(markerText()))
                throw new IOException("Migration record is invalid; existing files preserved");
            checkedStorage();
            if (!StoragePublish.syncDirectory(runtime)) throw new IOException("Cannot durably record storage migration; Start blocked");
            return;
        }
        File previous = new File(runtime,"ram-fmd.bin");
        unchangedPath(previous);
        // Resolve save ambiguity before publishing any migrated inputs.
        if (previous.exists()) {
            if (!previous.isFile() || previous.length()!=STORAGE_SIZE)
                throw new IOException("Private save size mismatch; original preserved");
            if (storage.exists() && (!storage.isFile() || storage.length()!=STORAGE_SIZE || !digest(previous).equals(digest(storage))))
                throw new IOException("Private and pocket2droid/data saves differ. Both preserved; choose which save to keep before Start.");
        }
        if (storage.exists()) checkedStorage();
        migrate(new File(runtime,"PPC_USA.BIN"), rom);
        migrate(new File(runtime,"RagingThunder.cab"), cab);
        if (!storage.exists()) {
            if (previous.exists()) migrate(previous, storage);
            else createStorage();
        }
        checkedStorage();
        // No engine is launched until this private checkpoint has been synced.
        // A crash before it retries identical completed copies. A crash after it
        // never compares a subsequently changed save against the rollback copy.
        byte[] record=markerText().getBytes(StandardCharsets.UTF_8);
        copyVerified(new ByteArrayInputStream(record), marker, record.length, file -> { });
        if (!StoragePublish.syncDirectory(runtime))
            throw new IOException("Cannot durably record storage migration; Start blocked");
    }
    private String markerText() throws IOException { return "Pocket2Droid external layout 1\n"+root.getCanonicalPath()+"\nsave=present\n"; }
    private static String readText(File file) throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try (InputStream in=new FileInputStream(file)) { byte[] b=new byte[4096]; int n; while ((n=in.read(b))!=-1) bytes.write(b,0,n); }
        return new String(bytes.toByteArray(),StandardCharsets.UTF_8);
    }
    private void checkedStorage() throws IOException {
        if (!storage.isFile() || storage.length()!=STORAGE_SIZE)
            throw new IOException("Persistent data/ram-fmd.bin missing or wrong size; retained files preserved, automatic reset blocked");
    }
    private static void directory(File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("Cannot create storage folder: "+folder);
    }
    private static void unchangedPath(File file) throws IOException {
        File expected=new File(file.getParentFile().getCanonicalFile(),file.getName());
        if (!file.getCanonicalFile().equals(expected)) throw new IOException("Redirected storage path refused: "+file);
    }
    private void migrate(File source, File target) throws IOException {
        unchangedPath(source); unchangedPath(target);
        if (!source.exists() || target.exists()) return;
        if (!source.isFile()) throw new IOException("Input is not a regular file: "+source);
        String expected=digest(source); long size=source.length();
        try (InputStream in=new FileInputStream(source)) {
            copyVerified(in,target,size,file -> {
                if (file.length()!=size || !digest(file).equals(expected) || !digest(source).equals(expected))
                    throw new IOException("Source changed or migration verification failed; original preserved");
            });
        }
    }
    private void createStorage() throws IOException {
        File pending=File.createTempFile("ram-fmd.bin.",".pending",data);
        try {
            try (RandomAccessFile out=new RandomAccessFile(pending,"rw")) { out.setLength(STORAGE_SIZE);out.getFD().sync(); }
            // Read back every page before publication, including a sparse first-run image.
            try (InputStream in=new FileInputStream(pending)) {
                byte[] b=new byte[1024*1024]; long size=0;int n;
                while ((n=in.read(b))!=-1) { size+=n;for(int i=0;i<n;i++) if(b[i]!=0) throw new IOException("New storage verification failed"); }
                if(size!=STORAGE_SIZE) throw new IOException("New storage verification failed");
            }
            StoragePublish.publish(pending,storage,runtime);StoragePublish.syncDirectory(data);
        } finally { StoragePublish.cleanupPending(pending,runtime); }
    }
    interface Validation { void check(File file) throws IOException; }
    private void copyVerified(InputStream in, File target, long limit, Validation validation) throws IOException {
        unchangedPath(target);
        File pending=File.createTempFile(target.getName()+".",".pending",target.getParentFile());
        try {
            MessageDigest hash=hash();long total=0;
            try (FileOutputStream out=new FileOutputStream(pending)) {
                byte[] b=new byte[1024*1024];int n;
                while((n=in.read(b))!=-1) {
                    total+=n;if(total>limit) throw new IOException("Selected input is too large");
                    out.write(b,0,n);hash.update(b,0,n);
                }
                out.getFD().sync();
            }
            String expected=hex(hash.digest());
            if(pending.length()!=total || !digest(pending).equals(expected)) throw new IOException("Copied input readback failed");
            validation.check(pending);
            if(target.exists()) {
                if(!target.isFile() || target.length()!=total || !digest(target).equals(expected))
                    throw new IOException("Existing "+target.getName()+" differs; preserved. Move it aside before importing a replacement.");
                return;
            }
            StoragePublish.publish(pending,target,runtime);StoragePublish.syncDirectory(target.getParentFile());
        } finally { StoragePublish.cleanupPending(pending,runtime); }
    }
    synchronized void importRom(InputStream in) throws IOException {
        prepare();copyVerified(in,rom,MAX_ROM_SIZE,file -> {
            if(file.length()==0) throw new IOException("Selected ROM is empty");
        });
    }
    synchronized void importCab(InputStream in) throws IOException {
        prepare();copyVerified(in,cab,64L*1024*1024,file -> {
            if(file.length()<36) throw new IOException("Short CAB");
            byte[] header=new byte[36];try(DataInputStream h=new DataInputStream(new FileInputStream(file))) { h.readFully(header); }
            ByteBuffer h=ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
            if(h.getInt(0)!=0x4643534d || (h.getInt(8)&0xffffffffL)!=file.length()) throw new IOException("Invalid CAB header");
        });
    }
    static void verifyRom(File rom) throws IOException {
        unchangedPath(rom);
        if(!rom.isFile() || !rom.canRead() || rom.length()==0)
            throw new IOException("ROM file is missing, empty or unreadable. Return to the library to import it.");
        if(rom.length()>MAX_ROM_SIZE) throw new IOException("ROM exceeds this emulated board's 96 MiB flash capacity");
    }
    private static MessageDigest hash() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch(NoSuchAlgorithmException impossible) { throw new AssertionError(impossible); }
    }
    static String digest(File file) throws IOException {
        MessageDigest d=hash();byte[] b=new byte[1024*1024];
        try(InputStream in=new FileInputStream(file)) { int n;while((n=in.read(b))!=-1)d.update(b,0,n); }
        return hex(d.digest());
    }
    private static String hex(byte[] hash) {
        char[] out=new char[hash.length*2];char[] digits="0123456789abcdef".toCharArray();
        for(int i=0;i<hash.length;i++) { out[i*2]=digits[(hash[i]&255)>>>4];out[i*2+1]=digits[hash[i]&15]; }
        return new String(out);
    }
}
