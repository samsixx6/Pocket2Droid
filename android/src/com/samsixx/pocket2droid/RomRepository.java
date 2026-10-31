package com.samsixx.pocket2droid;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;
import org.json.*;

final class RomRepository {
    enum SaveChoice { USE_EXISTING_COPY, NEW_INSTALLATION }
    static final String[] ICONS={"pda_blue","pda_teal","pda_amber","pda_violet","pda_orange","pda_red"};
    private static final Object lock=new Object();
    final File root,runtime,roms,data,defaultCard;
    private final Random random;
    RomRepository(File root,File runtime,File defaultCard){this(root,runtime,defaultCard,new Random());}
    RomRepository(File root,File runtime,File defaultCard,Random random){this.root=root;this.runtime=runtime;roms=new File(root,"Roms");data=new File(root,"data");this.defaultCard=defaultCard;this.random=random;}
    interface Work<T>{T run()throws IOException;}
    private <T>T work(Work<T> action)throws IOException {
        synchronized(lock){Object token=new Object();if(!SessionGuard.claim(token,null))throw new IOException("Exit the active session before changing the library");
            try{init();return action.run();}finally{SessionGuard.release(token);}}
    }
    private void init()throws IOException {
        ProfileFiles.dir(root);ProfileFiles.dir(runtime);ProfileFiles.dir(roms);ProfileFiles.dir(data);
        StoragePublish.recover(runtime,root);ProfileFiles.recover(root,runtime);RomUpdate.recover(root,runtime);recoverDelete();
    }
    static String label(String name){String s=name.replaceFirst("(?i)\\.bin$","");return s.trim().isEmpty()?"ROM":s.substring(0,Math.min(s.length(),180));}
    static String binName(String name)throws IOException {String safe=ProfileFiles.name(name);if(!safe.toLowerCase(Locale.ROOT).endsWith(".bin"))throw new IOException("Select a .bin ROM file. CAB and other file types cannot be imported here.");return safe;}
    private File folder(String filename)throws IOException{ProfileFiles.name(filename);File f=new File(data,filename);ProfileFiles.plain(f);return f;}
    private File metadata(String filename)throws IOException{return new File(folder(filename),"profile.json");}
    private String lastIcon()throws IOException {
        File f=new File(data,"library.json");if(!f.exists())return null;
        try{JSONObject o=ProfileFiles.object(f);if(o.getInt("schema")!=1)throw new IOException("Unknown library schema");String id=o.getString("lastAssignedIconId");if(!Arrays.asList(ICONS).contains(id))throw new IOException("Invalid library icon state");return id;}
        catch(JSONException e){throw new IOException("Invalid library state",e);}
    }
    private JSONObject record(RomSettings settings,String icon,String state)throws IOException {
        try{return settings.json().put("schema",1).put("iconId",icon).put("saveState",state);}catch(JSONException e){throw new IOException(e);}
    }
    private RomProfile load(String filename)throws IOException {
        ProfileFiles.name(filename);File rom=new File(roms,filename),save=new File(folder(filename),"ram-fmd.bin");ProfileFiles.plain(rom);ProfileFiles.plain(save);
        try{JSONObject o=ProfileFiles.object(metadata(filename));if(o.getInt("schema")!=1)throw new IOException("Unknown ROM profile schema");
            String icon=o.getString("iconId"),state=o.getString("saveState");if(!Arrays.asList(ICONS).contains(icon)||!Arrays.asList("NEW","READY","PREPARING_NEW","PREPARING_COPY").contains(state))throw new IOException("Invalid profile state");
            RomSettings settings=RomSettings.parse(o);
            if(settings.resolution==RomSettings.GuestResolution.ROM_DEFAULT)settings=settings.withResolution(settings.startupResolution(filename));
            if(o.has("settingsKey")&&!(o.get("settingsKey") instanceof String))throw new IOException("Invalid installation settings key");
            String key=o.optString("settingsKey",filename);ProfileFiles.name(key);return new RomProfile(filename,icon,rom,save,settings,state,null,key);
        }catch(JSONException e){throw new IOException("Invalid ROM profile; existing save preserved",e);}
    }
    private RomProfile add(String name,String display)throws IOException {
        File meta=metadata(name);if(meta.exists())return load(name);
        File existing=new File(folder(name),"ram-fmd.bin");ProfileFiles.plain(existing);
        String state="NEW";
        if(folder(name).exists()){
            if(!existing.isFile()||existing.length()!=StorageLayout.STORAGE_SIZE)throw new IOException("Existing data folder lacks a valid save; preserved for recovery: "+name);
            state="READY";
        }
        File card=defaultCard;
        if(isManagedDefault(card)&&!card.exists())ProfileFiles.dir(card);
        card=validateCard(card);String last=lastIcon();List<String> choices=new ArrayList<>(Arrays.asList(ICONS));choices.remove(last);String icon=choices.get(random.nextInt(choices.size()));
        RomSettings settings=RomSettings.defaults(display,card);
        if(state.equals("READY"))settings=settings.withNetworkSetupEligibility(false);
        settings=settings.withResolution(settings.startupResolution(name));
        try{JSONObject library=new JSONObject().put("schema",1).put("lastAssignedIconId",icon);
            ProfileFiles.commit(root,runtime,ProfileFiles.update("data/"+name+"/profile.json",state.equals("NEW")?record(settings,icon,state).put("settingsKey","rom-"+UUID.randomUUID().toString().replace("-","")):record(settings,icon,state)),ProfileFiles.update("data/library.json",library));
        }catch(JSONException e){throw new IOException(e);}return load(name);
    }
    private List<String> allNames()throws IOException {
        File[] files=roms.listFiles();if(files==null)throw new IOException("ROM folder unavailable");
        // A list preserves distinct case-only names on case-sensitive volumes.
        List<String> all=new ArrayList<>();for(File f:files)if(f.isFile()&&!f.getName().startsWith(".")&&f.getName().toLowerCase(Locale.ROOT).endsWith(".bin"))all.add(f.getName());
        File[] dirs=data.listFiles();if(dirs==null)throw new IOException("Data folder unavailable");for(File f:dirs)if(f.isDirectory()&&!f.getName().startsWith(".")&&new File(f,"profile.json").exists()&&!all.contains(f.getName()))all.add(f.getName());
        Collections.sort(all,String.CASE_INSENSITIVE_ORDER);return all;
    }
    private List<String> savedOrder()throws IOException {
        File file=new File(data,"library-order.json");ProfileFiles.plain(file);
        List<String> order=new ArrayList<>();if(!file.exists())return order;
        try{JSONObject state=ProfileFiles.object(file);if(state.getInt("schema")!=1)throw new IOException("Unknown library order schema");
            JSONArray names=state.getJSONArray("roms");Set<String> seen=new HashSet<>();
            for(int i=0;i<names.length();i++){String name=ProfileFiles.name(names.getString(i));if(!seen.add(name))throw new IOException("Duplicate ROM in library order");order.add(name);}
            return order;
        }catch(JSONException failure){throw new IOException("Invalid library order; existing data preserved",failure);}
    }
    private List<String> orderedNames(List<String> all)throws IOException {
        Set<String> remaining=new LinkedHashSet<>(all);List<String> result=new ArrayList<>();
        for(String name:savedOrder())if(remaining.remove(name))result.add(name);
        result.addAll(remaining);return result;
    }
    private void saveOrder(List<String> order)throws IOException {
        // Unsupported external filenames keep their individual error rows, but
        // must never make the persistent ledger unreadable on the next scan.
        List<String> valid=new ArrayList<>();for(String name:order){try{ProfileFiles.name(name);valid.add(name);}catch(IOException unsupported){}}
        if(savedOrder().equals(valid))return;
        try{JSONObject state=new JSONObject().put("schema",1).put("roms",new JSONArray(valid));
            ProfileFiles.commit(root,runtime,ProfileFiles.update("data/library-order.json",state));
        }catch(JSONException failure){throw new IOException(failure);}
    }
    List<RomProfile> scan()throws IOException{return work(()->{
        List<String> all=orderedNames(allNames());List<RomProfile> profiles=new ArrayList<>();
        for(String name:all){try{RomProfile p=add(name,label(name));String error=p.rom.isFile()?null:"ROM file unavailable";profiles.add(new RomProfile(name,p.iconId,p.rom,p.storage,p.settings,p.saveState,error,p.settingsKey));}
            catch(IOException failure){RomSettings fallback=RomSettings.defaults(label(name),defaultCard);profiles.add(new RomProfile(name,ICONS[0],new File(roms,name),new File(data,name+"/ram-fmd.bin"),fallback,"INVALID",failure.getMessage()));}}
        saveOrder(all);return profiles;
    });}
    RomProfile importRom(InputStream input,String sourceName)throws IOException{return work(()->{
        String source=binName(sourceName);validateDefault();List<String> order=orderedNames(allNames());saveOrder(order);String selected=source;int n=2;
        int dot=source.lastIndexOf('.');String stem=dot>0?source.substring(0,dot):source,ext=dot>0?source.substring(dot):"";
        while(new File(roms,selected).exists()||folder(selected).exists()){selected=stem+" ("+(n++)+")"+ext;ProfileFiles.name(selected);}
        File target=new File(roms,selected);copy(input,target,StorageLayout.MAX_ROM_SIZE,false,null);
        RomProfile profile=add(selected,label(source));order.add(selected);saveOrder(order);return profile;
    });}
    static String cardFileName(String name)throws IOException {
        if(name==null||name.isEmpty()||name.equals(".")||name.equals("..")||!name.equals(name.trim())||name.contains("/")||name.contains("\\")||name.indexOf('\0')>=0||name.indexOf('\n')>=0||name.indexOf('\r')>=0||name.getBytes(StandardCharsets.UTF_8).length>180)
            throw new IOException("Selected file has an unsupported filename");
        return name;
    }
    File importCardFile(InputStream input,String sourceName)throws IOException{return work(()->{
        String source=cardFileName(sourceName);File card=new File(root,"storagecard");ProfileFiles.dir(card);card=validateCard(card);
        String selected=source;int suffix=1,dot=source.lastIndexOf('.');
        String stem=dot>0?source.substring(0,dot):source,extension=dot>0?source.substring(dot):"";
        while(new File(card,selected).exists())selected=cardFileName(stem+" ("+(suffix++)+")"+extension);
        File target=new File(card,selected);copy(input,target,Long.MAX_VALUE,false,null,true);return target;
    });}
    private boolean isManagedDefault(File card){return card.equals(new File(root,"storagecard"))||card.equals(new File(root,"storage"));}
    private void validateDefault()throws IOException{if(isManagedDefault(defaultCard)&&!defaultCard.exists())ProfileFiles.dir(defaultCard);validateCard(defaultCard);}
    File validateCard(File card)throws IOException {
        File c=validateCardPath(card);
        if(!c.isDirectory()||!c.canRead()||!c.canWrite())throw new IOException("storagecard folder is unavailable or not writable");
        return c;
    }
    private File validateCardPath(File card)throws IOException {
        if(card==null||!card.isAbsolute())throw new IOException("Select an accessible filesystem folder");
        ProfileFiles.plain(card);File c=card.getCanonicalFile();
        for(File managed:new File[]{roms,data,runtime})if(overlap(c,managed.getCanonicalFile()))throw new IOException("Use a dedicated folder outside ROM, data and runtime folders");
        return c;
    }
    private static boolean overlap(File a,File b){String x=a.getPath(),y=b.getPath();return x.equals(y)||x.startsWith(y+File.separator)||y.startsWith(x+File.separator);}
    RomProfile verifyForLaunch(String name)throws IOException{return work(()->{RomProfile p=load(name);verifyFiles(p);validateCard(p.settings.card);return p;});}
    RomProfile getProfile(String name)throws IOException{return work(()->load(RomUpdate.resolveUpdatedName(root,runtime,name)));}
    RomProfile updateRom(String name,InputStream input,String sourceName,RomUpdate.Identity identity)throws IOException{return updateRom(name,null,input,sourceName,identity);}
    private RomProfile selected(String name,String expectedKey)throws IOException{RomProfile p=load(name);if(expectedKey!=null&&!expectedKey.equals(p.settingsKey))throw new IOException("The selected ROM installation has changed; reopen its settings");return p;}
    RomProfile updateRom(String name,String expectedKey,InputStream input,String sourceName,RomUpdate.Identity identity)throws IOException{return work(()->{
        RomProfile p=selected(name,expectedKey);String next=binName(sourceName);RomUpdate.replace(root,runtime,p,input,next,orderedNames(allNames()),identity);return load(next);
    });}
    // Called only by Engine while SessionGuard protects its storage ownership.
    RomProfile verifySessionProfile(String name)throws IOException{init();RomProfile p=load(name);verifyFiles(p);validateCard(p.settings.card);return p;}
    static void verifyFiles(RomProfile p)throws IOException {
        ProfileFiles.plain(p.rom);ProfileFiles.plain(p.storage.getParentFile());ProfileFiles.plain(p.storage);StorageLayout.verifyRom(p.rom);
        if(!p.saveState.equals("READY")||!p.storage.isFile()||p.storage.length()!=StorageLayout.STORAGE_SIZE)throw new IOException("Internal storage is not prepared, missing or invalid; automatic reset blocked");
        if(!p.settings.resolution.supported())throw new IOException("Guest resolution override is not verified for this engine; choose ROM default");
    }
    boolean needsPreparation(String name)throws IOException{return work(()->!load(name).saveState.equals("READY"));}
    boolean hasLegacySave()throws IOException{return work(()->legacy()!=null);}
    private File legacy()throws IOException{
        File f=new File(data,"ram-fmd.bin");if(!f.exists())f=new File(runtime,"ram-fmd.bin");if(!f.exists())return null;ProfileFiles.plain(f);if(!f.isFile()||f.length()!=StorageLayout.STORAGE_SIZE)throw new IOException("Existing unassigned internal data has an invalid size; original preserved");return f;
    }
    void prepareSave(String name,SaveChoice choice)throws IOException{work(()->{
        RomProfile p=load(name);StorageLayout.verifyRom(p.rom);
        if(p.saveState.equals("READY")){verifyFiles(p);return null;}
        boolean copying=p.saveState.equals("PREPARING_COPY")||(p.saveState.equals("NEW")&&choice==SaveChoice.USE_EXISTING_COPY);
        File old=copying?legacy():null;if(copying&&old==null)throw new IOException("No existing unassigned data to copy");
        String state=copying?"PREPARING_COPY":"PREPARING_NEW";
        RomSettings preparedSettings=copying?p.settings.withNetworkSetupEligibility(false):p.settings;
        saveRecord(p,record(preparedSettings,p.iconId,state));
        String expected=old==null?zeroDigest():StorageLayout.digest(old);
        if(p.storage.exists()){
            if(!p.storage.isFile()||p.storage.length()!=StorageLayout.STORAGE_SIZE||!StorageLayout.digest(p.storage).equals(expected))throw new IOException("Existing internal data differs from the pending preparation; both preserved");
        }else if(old!=null){try(InputStream in=new FileInputStream(old)){copy(in,p.storage,StorageLayout.STORAGE_SIZE,true,expected);}if(!StorageLayout.digest(old).equals(expected))throw new IOException("Original data changed during copy; both preserved");}
        else{InputStream zero=new InputStream(){long left=StorageLayout.STORAGE_SIZE;public int read(){if(left--<=0)return -1;return 0;}public int read(byte[] b,int off,int len){if(left==0)return -1;int n=(int)Math.min(left,len);Arrays.fill(b,off,off+n,(byte)0);left-=n;return n;}};copy(zero,p.storage,StorageLayout.STORAGE_SIZE,true,expected);}
        saveRecord(p,record(preparedSettings,p.iconId,"READY"));return null;
    });}
    private static String zeroDigest()throws IOException{try{MessageDigest d=MessageDigest.getInstance("SHA-256");byte[] b=new byte[1024*1024];for(int i=0;i<32;i++)d.update(b);return hex(d.digest());}catch(NoSuchAlgorithmException e){throw new IOException(e);}}
    private void saveRecord(RomProfile p,JSONObject o)throws IOException{try{o.put("settingsKey",p.settingsKey);}catch(JSONException e){throw new IOException(e);}ProfileFiles.commit(root,runtime,ProfileFiles.update("data/"+p.filename+"/profile.json",o));}
    void saveSettings(String name,RomSettings settings)throws IOException{saveSettings(name,null,settings);}
    void saveSettings(String name,String expectedKey,RomSettings settings)throws IOException{work(()->{RomProfile p=selected(name,expectedKey);validateCard(settings.card);if(!settings.resolution.supported())throw new IOException("Choose ROM default; guest overrides are not yet verified");saveRecord(p,record(settings,p.iconId,p.saveState));return null;});}
    private void copy(InputStream input,File target,long limit,boolean exact,String expected)throws IOException {
        copy(input,target,limit,exact,expected,false);
    }
    private void copy(InputStream input,File target,long limit,boolean exact,String expected,boolean allowEmpty)throws IOException {
        ProfileFiles.plain(target);ProfileFiles.dir(target.getParentFile());String prefix=target.getName()+".";while(prefix.length()<3)prefix+="_";File pending=File.createTempFile(prefix,".pending",target.getParentFile());
        try{MessageDigest hash=MessageDigest.getInstance("SHA-256");long count=0;try(FileOutputStream out=new FileOutputStream(pending)){byte[] b=new byte[1024*1024];int n;while((n=input.read(b))!=-1){if(n==0)continue;if(n>limit-count)throw new IOException("Selected file exceeds the supported capacity");count+=n;out.write(b,0,n);hash.update(b,0,n);}out.getFD().sync();}
            String digest=hex(hash.digest());if((count==0&&!allowEmpty)||(exact&&count!=limit)||!StorageLayout.digest(pending).equals(digest)||(expected!=null&&!expected.equals(digest)))throw new IOException("Copied input verification failed");
            StoragePublish.publish(pending,target,runtime);if(!StoragePublish.syncDirectory(target.getParentFile()))throw new IOException("Cannot verify durable publication");
        }catch(NoSuchAlgorithmException e){throw new IOException(e);}finally{StoragePublish.cleanupPending(pending,runtime);}
    }
    private static String hex(byte[] b){StringBuilder s=new StringBuilder();for(byte x:b)s.append(String.format(Locale.ROOT,"%02x",x&255));return s.toString();}
    void deleteRom(String name)throws IOException{deleteRom(name,null);}
    void deleteRom(String name,String expectedKey)throws IOException{work(()->{if(expectedKey!=null)selected(name,expectedKey);ProfileFiles.name(name);File rom=new File(roms,name);ProfileFiles.plain(rom);if(!rom.exists()&&!folder(name).exists())throw new IOException("ROM installation no longer exists");folder(name);checkDeleteCards();File j=new File(runtime,"library-delete.pending.json");try{ProfileFiles.publishJournal(j,new JSONObject().put("schema",1).put("filename",name).toString(),runtime);}catch(JSONException e){throw new IOException(e);}recoverDelete();return null;});}
    private void checkDeleteCards()throws IOException {
        validateCardPath(defaultCard);File[] dirs=data.listFiles();if(dirs==null)throw new IOException("Cannot inspect card assignments");
        for(File d:dirs)if(d.isDirectory()&&new File(d,"profile.json").exists()){ProfileFiles.plain(d);RomProfile p=load(d.getName());validateCardPath(p.settings.card);}
    }
    private void recoverDelete()throws IOException {
        File j=new File(runtime,"library-delete.pending.json");ProfileFiles.plain(j);if(!j.exists())return;
        try{JSONObject o=ProfileFiles.object(j);if(o.getInt("schema")!=1)throw new IOException("Invalid deletion record");String name=ProfileFiles.name(o.getString("filename"));checkDeleteCards();List<String> order=savedOrder();File rom=new File(roms,name),folder=folder(name);inspectTree(folder);ProfileFiles.plain(rom);
            if(rom.exists()&&(!rom.isFile()||!rom.delete()))throw new IOException("ROM deletion incomplete; internal data retained");
            removeTree(folder);order.remove(name);saveOrder(order);if(!StoragePublish.syncDirectory(roms)||!StoragePublish.syncDirectory(data)||!j.delete()||!StoragePublish.syncDirectory(runtime))throw new IOException("Deletion checkpoint incomplete; retry required");
        }catch(JSONException e){throw new IOException("Invalid deletion record; files preserved",e);}
    }
    private static void inspectTree(File f)throws IOException{ProfileFiles.plain(f);if(f.isDirectory()){File[] children=f.listFiles();if(children==null)throw new IOException("Cannot inspect internal folder");for(File c:children)inspectTree(c);}}
    private static void removeTree(File f)throws IOException{ProfileFiles.plain(f);if(f.isDirectory()){File[] children=f.listFiles();if(children==null)throw new IOException("Cannot read internal folder");for(File c:children)removeTree(c);}if(f.exists()&&!f.delete())throw new IOException("Internal data deletion incomplete; retry required");}
}
