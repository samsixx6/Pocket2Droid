package com.samsixx.pocket2droid;
import java.io.*;import java.nio.charset.StandardCharsets;import java.util.*;import org.json.*;

/** One recoverable ROM/data identity replacement. Never prepares or zeros a save. */
final class RomUpdate {
 interface Identity {String prepare(RomProfile current)throws IOException;}
 private static final String JOURNAL="library-rom-update.pending.json", OWNER="owner.txt";
 private static final int MAX_ENTRIES=4096;
 private RomUpdate(){}
 private static IOException bad(String s){return new IOException(s+"; existing installation and backup preserved");}
 private static File journal(File runtime){return new File(runtime,JOURNAL);}
 private static void sync(File dir)throws IOException{if(!StoragePublish.syncDirectory(dir))throw bad("Cannot confirm ROM update durability");}
 private static void directory(File f)throws IOException{ProfileFiles.plain(f);if(!f.isDirectory()){if(!f.mkdir())throw bad("Cannot create update directory");sync(f.getParentFile());}}
 private static String digest(File p)throws IOException{ProfileFiles.plain(p);if(!p.isFile())throw bad("Update file is unavailable: "+p.getName());return StorageLayout.digest(p);}
 private static String rootName(File root)throws IOException{return root.getCanonicalPath();}
 private static boolean entryExists(File f)throws IOException{if(f.exists())return true;String[] names=f.getParentFile().list();if(names==null)throw bad("Cannot inspect update destination");for(String n:names)if(n.equals(f.getName()))return true;return false;}
 private static File history(File root)throws IOException{File p=new File(new File(root,"data"),".rom-updates");ProfileFiles.plain(p);return p;}
 private static File stage(File root,String id)throws IOException{if(!id.matches("[0-9a-f]{32}"))throw bad("Invalid update identity");File p=new File(history(root),id);ProfileFiles.plain(p);return p;}
 private static String owner(File root,String id)throws IOException{return "Pocket2Droid ROM update 1\n"+rootName(root)+"\n"+id+"\n";}
 private static String[] parts(String relative)throws IOException{
  if(relative==null||relative.isEmpty()||relative.length()>2048||relative.indexOf('\\')>=0)throw bad("Invalid saved-data path");String[] parts=relative.split("/",-1);if(parts.length>16)throw bad("Saved-data nesting is too deep");for(String p:parts)if(p.isEmpty()||p.equals(".")||p.equals("..")||p.indexOf('\0')>=0||p.indexOf('\n')>=0||p.indexOf('\r')>=0)throw bad("Invalid saved-data filename");return parts;
 }
 private static File child(File base,String relative)throws IOException{
  File f=base;ProfileFiles.plain(f);for(String p:parts(relative)){f=new File(f,p);ProfileFiles.plain(f);}return f;
 }
 private static byte[] utf(String s){return s.getBytes(StandardCharsets.UTF_8);}
 private static void freshText(File file,String text)throws IOException{
  ProfileFiles.plain(file);if(!file.createNewFile())throw bad("Update checkpoint already exists");try(FileOutputStream out=new FileOutputStream(file)){out.write(utf(text));out.getFD().sync();}if(!ProfileFiles.read(file).equals(text))throw bad("Update checkpoint readback failed");sync(file.getParentFile());
 }
 private static void syncFile(File file)throws IOException{try(RandomAccessFile stable=new RandomAccessFile(file,"rw")){stable.getFD().sync();}sync(file.getParentFile());}
 private static void expectedText(File file,String text)throws IOException{
  ProfileFiles.plain(file);if(file.exists()){String have=ProfileFiles.read(file);if(!have.equals(text)){if(!text.startsWith(have))throw bad("Update checkpoint changed");if(!file.delete())throw bad("Cannot retry interrupted update checkpoint");sync(file.getParentFile());freshText(file,text);}syncFile(file);return;}freshText(file,text);
 }
 private static void checkOwner(File root,File operation,String id)throws IOException{if(!operation.isDirectory()||!ProfileFiles.read(new File(operation,OWNER)).equals(owner(root,id)))throw bad("Invalid update directory ownership");}
 private static JSONObject fileInfo(File f)throws IOException{try{return new JSONObject().put("size",f.length()).put("sha256",digest(f));}catch(JSONException e){throw new IOException(e);}}
 private static boolean matches(File f,JSONObject expected)throws IOException{
  ProfileFiles.plain(f);try{return f.isFile()&&f.length()==expected.getLong("size")&&digest(f).equals(expected.getString("sha256"));}catch(JSONException e){throw bad("Invalid update file identity");}
 }
 private static void validateFileInfo(JSONObject o)throws IOException,JSONException{if(!(o.get("size") instanceof Number)||o.getLong("size")<0||!o.getString("sha256").matches("[0-9a-f]{64}"))throw bad("Invalid update digest");}
 private static JSONArray inventory(File directory)throws IOException{JSONArray out=new JSONArray();walk(directory,"",out);return out;}
 private static void walk(File dir,String prefix,JSONArray out)throws IOException{
  ProfileFiles.plain(dir);if(!dir.isDirectory())throw bad("Saved-data folder is unavailable");File[] files=dir.listFiles();if(files==null)throw bad("Cannot inspect saved data");Arrays.sort(files,Comparator.comparing(File::getName));
  for(File f:files){if(out.length()>=MAX_ENTRIES)throw bad("Saved-data inventory is too large");String rel=prefix+f.getName();child(directoryBase(dir,prefix),rel);ProfileFiles.plain(f);
   try{JSONObject row=new JSONObject().put("path",rel).put("directory",f.isDirectory());if(f.isDirectory()){out.put(row);walk(f,rel+"/",out);}else if(f.isFile()){JSONObject info=fileInfo(f);row.put("size",info.getLong("size")).put("sha256",info.getString("sha256"));out.put(row);}else throw bad("Unsupported saved-data file");}
   catch(JSONException e){throw new IOException(e);}
  }
 }
 private static File directoryBase(File dir,String prefix){int count=0;for(int i=0;i<prefix.length();i++)if(prefix.charAt(i)=='/')count++;while(count-->0)dir=dir.getParentFile();return dir;}
 private static Map<String,JSONObject> rows(JSONArray list)throws IOException,JSONException{
  if(list.length()<1||list.length()>MAX_ENTRIES)throw bad("Invalid saved-data inventory");Map<String,JSONObject> result=new LinkedHashMap<>();
  for(int i=0;i<list.length();i++){JSONObject row=list.getJSONObject(i);String p=row.getString("path");parts(p);if(!(row.get("directory") instanceof Boolean)||result.put(p,row)!=null)throw bad("Duplicate or invalid saved-data path");if(!row.getBoolean("directory"))validateFileInfo(row);}
  JSONObject meta=result.get("profile.json");if(meta==null||meta.getBoolean("directory"))throw bad("Update lacks the original profile");return result;
 }
 private static void verifyTree(File dir,JSONArray inventory,boolean allowMissing)throws IOException{
  try{Map<String,JSONObject> want=rows(inventory);if(!dir.exists()){if(allowMissing)return;throw bad("Saved-data snapshot missing");}JSONArray actual=inventory(dir);Set<String> seen=new HashSet<>();
   for(int i=0;i<actual.length();i++){JSONObject have=actual.getJSONObject(i);String path=have.getString("path");JSONObject row=want.get(path);if(row==null||row.getBoolean("directory")!=have.getBoolean("directory")||(!row.getBoolean("directory")&&!matches(child(dir,path),row)))throw bad("Saved data changed during update: "+path);seen.add(path);}
   if(!allowMissing&&seen.size()!=want.size())throw bad("Saved-data snapshot is incomplete");
  }catch(JSONException e){throw bad("Invalid saved-data snapshot");}
 }
 private static void copyTreeFresh(File source,File target,JSONArray items)throws IOException{
  directory(target);try{for(int i=0;i<items.length();i++){JSONObject row=items.getJSONObject(i);File to=child(target,row.getString("path"));if(row.getBoolean("directory"))directory(to);else{StoragePublish.copyNewFile(child(source,row.getString("path")),to);if(!matches(to,row))throw bad("Saved-data backup readback failed");sync(to.getParentFile());}}sync(target);}catch(JSONException e){throw new IOException(e);}
 }
 private static boolean prefixMatches(File from,File to)throws IOException{
  if(to.length()>from.length())return false;try(InputStream a=new FileInputStream(from);InputStream b=new FileInputStream(to)){byte[] x=new byte[65536],y=new byte[65536];int n;while((n=b.read(y))!=-1){int done=0;while(done<n){int got=a.read(x,done,n-done);if(got<0)return false;done+=got;}for(int i=0;i<n;i++)if(x[i]!=y[i])return false;}return true;}
 }
 /* Only a journal-owned NEW output may be retried. Never rewrite a full or
    differing existing file, and never write into the authoritative old save. */
 private static void copyOrResume(File from,File to,JSONObject info)throws IOException{
  ProfileFiles.plain(to);try{if(to.exists()){if(matches(to,info)){syncFile(to);return;}if(!to.isFile()||to.length()>=info.getLong("size")||!prefixMatches(from,to))throw bad("Update output differs: "+to.getName());if(!to.delete())throw bad("Cannot retire partial update output");sync(to.getParentFile());}
   StoragePublish.copyNewFile(from,to);if(!matches(to,info))throw bad("Update copy readback failed");sync(to.getParentFile());
  }catch(JSONException e){throw new IOException(e);}
 }
 private static boolean download(File f)throws IOException{byte[] h=new byte[7];try(InputStream in=new FileInputStream(f)){int count=0;while(count<7){int n=in.read(h,count,7-count);if(n<0)return false;count+=n;}}return Arrays.equals(h,new byte[]{'B','0','0','0','F','F','\n'});}
 private static void compatibility(RomProfile current,File candidate,String oldHash,String newHash)throws IOException{
  boolean before=download(current.rom),after=download(candidate);if(before&&current.saveState.equals("READY")){File ram=new File(current.storage.getParentFile(),"wm5-sdram-"+oldHash+".bin"),ready=new File(ram.getPath()+".ready");ProfileFiles.plain(ram);ProfileFiles.plain(ready);String checkpoint="Pocket2Droid WM5 RAM 1\n"+ram.getName()+"\n"+GuestRam.SIZE+"\n";if(!ram.isFile()||ram.length()!=GuestRam.SIZE||!ready.isFile()||!ProfileFiles.read(ready).equals(checkpoint))throw bad("Required WM5 retained RAM or checkpoint is missing or invalid");}if(current.saveState.equals("READY")&&before!=after)throw bad("This update changes the installation storage format");
  if(before&&!oldHash.equals(newHash)){File[] retained=current.storage.getParentFile().listFiles((d,n)->n.startsWith("wm5-sdram-")&&(n.endsWith(".bin")||n.endsWith(".ready")));if(retained==null)throw bad("Cannot inspect WM5 retained state");if(retained.length>0)throw bad("Changed WM5 ROM cannot safely reuse hash-bound saved RAM");}
 }
 private static void stream(InputStream in,File to)throws IOException{
  if(!to.createNewFile())throw bad("Prepared ROM input already exists");long count=0;try(FileOutputStream out=new FileOutputStream(to)){byte[] b=new byte[1024*1024];int n;while((n=in.read(b))!=-1){if(n==0)continue;if(n>StorageLayout.MAX_ROM_SIZE-count)throw new IOException("ROM exceeds the supported 96 MiB flash capacity");out.write(b,0,n);count+=n;}out.getFD().sync();}if(count==0)throw new IOException("Selected ROM is empty");StorageLayout.verifyRom(to);sync(to.getParentFile());
 }
 private static void emptyOrOwned(File dir,File root,String id)throws IOException{
  ProfileFiles.plain(dir);if(!dir.exists()){directory(dir);sync(dir.getParentFile());}File marker=new File(dir,".p2d-update-"+id);ProfileFiles.plain(marker);
  if(!marker.exists()){String[] names=dir.list();if(names==null||names.length!=0)throw bad("New data directory belongs to another installation");expectedText(marker,owner(root,id));}
  else if(!ProfileFiles.read(marker).equals(owner(root,id))){
   String partial=ProfileFiles.read(marker),expected=owner(root,id);String[] names=dir.list();
   if(names==null||names.length!=1||!names[0].equals(marker.getName())||!expected.startsWith(partial))throw bad("New data directory ownership changed");
   if(!marker.delete())throw bad("Cannot retry interrupted update ownership");sync(dir);freshText(marker,expected);
  }
 }
 static void replace(File root,File runtime,RomProfile current,InputStream input,String next,List<String> order,Identity identity)throws IOException{
  RomRepository.binName(next);ProfileFiles.plain(current.rom);StorageLayout.verifyRom(current.rom);if(!current.saveState.equals("READY")&&!current.saveState.equals("NEW"))throw bad("Finish pending installation preparation before changing the ROM");if(current.saveState.equals("READY"))RomRepository.verifyFiles(current);
  File target=new File(new File(root,"Roms"),next),targetData=new File(new File(root,"data"),next);ProfileFiles.plain(target);ProfileFiles.plain(targetData);boolean same=next.equals(current.filename);
  if(!same&&(entryExists(target)||entryExists(targetData)))throw bad("Another ROM or data folder already uses "+next);if(journal(runtime).exists())throw bad("A ROM update is already pending");
  String id=UUID.randomUUID().toString().replace("-","");directory(history(root));File op=stage(root,id);directory(op);freshText(new File(op,OWNER),owner(root,id));boolean published=false;
  try{File after=new File(op,"rom-after.bin");stream(input,after);String oldHash=digest(current.rom),newHash=digest(after);compatibility(current,after,oldHash,newHash);if(same&&oldHash.equals(newHash))return;
   JSONArray inventory=inventory(current.storage.getParentFile());File before=new File(op,"rom-before.bin");StoragePublish.copyNewFile(current.rom,before);sync(op);if(!digest(before).equals(oldHash))throw bad("Original ROM changed during backup");copyTreeFresh(current.storage.getParentFile(),new File(op,"before-data"),inventory);verifyTree(current.storage.getParentFile(),inventory,false);
   if(!same&&(entryExists(target)||entryExists(targetData)))throw bad("ROM update destination appeared during preparation");
   String settingsKey=same?current.settingsKey:ProfileFiles.name(identity.prepare(current));
   try{JSONObject metadata=ProfileFiles.object(new File(op,"before-data/profile.json"));metadata.put("displayName",RomRepository.label(next)).put("settingsKey",settingsKey);JSONArray names=new JSONArray();Set<String> unique=new HashSet<>();for(String name:order){try{ProfileFiles.name(name);}catch(IOException invalid){continue;}String value=name.equals(current.filename)?next:name;if(!unique.add(value))throw bad("Conflicting library order");names.put(value);}if(!unique.contains(next))throw bad("Current installation is missing from library order");
    File orderFile=new File(new File(root,"data"),"library-order.json");ProfileFiles.plain(orderFile);JSONObject record=new JSONObject().put("schema",1).put("root",rootName(root)).put("id",id).put("old",current.filename).put("next",next).put("oldRom",fileInfo(before)).put("newRom",fileInfo(after)).put("inventory",inventory).put("profileText",metadata.toString()).put("orderText",new JSONObject().put("schema",1).put("roms",names).toString()).put("orderBefore",orderFile.exists()?digest(orderFile):JSONObject.NULL).put("settingsKey",settingsKey).put("priorKey",current.settingsKey).put("aliasSequence",nextSequence(root,runtime));
    if(utf(record.toString()).length>900000)throw bad("ROM update recovery record is too large");ProfileFiles.publishJournal(journal(runtime),record.toString(),runtime);published=true;recover(root,runtime);
   }catch(JSONException e){throw new IOException(e);}
  }finally{if(!published&&!journal(runtime).exists())removeOwnedStage(root,op,id);}
 }
 private static void removeOwnedStage(File root,File op,String id)throws IOException{checkOwner(root,op,id);removeTree(op);sync(op.getParentFile());}
 private static void removeTree(File p)throws IOException{ProfileFiles.plain(p);if(p.isDirectory()){File[] files=p.listFiles();if(files==null)throw bad("Cannot inspect uncommitted update");for(File f:files)removeTree(f);}if(p.exists()&&!p.delete())throw bad("Cannot remove uncommitted update input");}
 private static JSONObject readRecord(File root,File runtime)throws IOException{
  try{JSONObject r=ProfileFiles.object(journal(runtime));if(r.getInt("schema")!=1||!r.getString("root").equals(rootName(root)))throw bad("Invalid ROM update checkpoint");String id=r.getString("id");stage(root,id);RomRepository.binName(r.getString("old"));RomRepository.binName(r.getString("next"));validateFileInfo(r.getJSONObject("oldRom"));validateFileInfo(r.getJSONObject("newRom"));if(r.getJSONObject("newRom").getLong("size")<1||r.getJSONObject("newRom").getLong("size")>StorageLayout.MAX_ROM_SIZE)throw bad("Invalid prepared ROM size");rows(r.getJSONArray("inventory"));ProfileFiles.name(r.getString("settingsKey"));ProfileFiles.name(r.getString("priorKey"));if(r.getLong("aliasSequence")<1)throw bad("Invalid update sequence");if(!r.isNull("orderBefore")&&!r.getString("orderBefore").matches("[0-9a-f]{64}"))throw bad("Invalid library identity");JSONObject meta=new JSONObject(r.getString("profileText")),order=new JSONObject(r.getString("orderText"));RomSettings.parse(meta);if(meta.getInt("schema")!=1||!meta.getString("settingsKey").equals(r.getString("settingsKey"))||!meta.getString("displayName").equals(RomRepository.label(r.getString("next")))||order.getInt("schema")!=1)throw bad("Invalid replacement metadata");Set<String> seen=new HashSet<>();JSONArray ns=order.getJSONArray("roms");for(int i=0;i<ns.length();i++)if(!seen.add(ProfileFiles.name(ns.getString(i))))throw bad("Invalid replacement order");if(!seen.contains(r.getString("next")))throw bad("Replacement order lacks updated ROM");return r;
  }catch(JSONException e){throw bad("Invalid ROM update recovery record");}
 }
 static void recover(File root,File runtime)throws IOException{
  File pending=journal(runtime);ProfileFiles.plain(pending);if(!pending.exists())return;JSONObject r=readRecord(root,runtime);
  try{String id=r.getString("id"),old=r.getString("old"),next=r.getString("next");boolean same=old.equals(next);File op=stage(root,id);checkOwner(root,op,id);File before=new File(op,"rom-before.bin"),after=new File(op,"rom-after.bin"),snapshot=new File(op,"before-data");JSONArray items=r.getJSONArray("inventory");if(!matches(before,r.getJSONObject("oldRom"))||!matches(after,r.getJSONObject("newRom")))throw bad("ROM update snapshot changed");verifyTree(snapshot,items,false);
   JSONObject oldMeta=ProfileFiles.object(new File(snapshot,"profile.json"));JSONObject expected=new JSONObject(oldMeta.toString()).put("displayName",RomRepository.label(next)).put("settingsKey",r.getString("settingsKey"));if(!sameJSON(expected,new JSONObject(r.getString("profileText"))))throw bad("Replacement changed unrelated settings");
   File oldData=new File(new File(root,"data"),old),newData=new File(new File(root,"data"),next),rom=new File(new File(root,"Roms"),next);ProfileFiles.plain(oldData);ProfileFiles.plain(newData);ProfileFiles.plain(rom);
   if(!same){File marker=new File(newData,".p2d-update-"+id),completed=new File(op,"completed.txt");ProfileFiles.plain(marker);ProfileFiles.plain(completed);
    if(!marker.exists()&&completed.exists()){expectedText(completed,"Updated "+old+" -> "+next+"\n");verifyCurrentData(newData,items,r.getString("profileText"));}
    else emptyOrOwned(newData,root,id);
    for(int i=0;i<items.length();i++){JSONObject row=items.getJSONObject(i);String path=row.getString("path");if(path.equals("profile.json"))continue;File to=child(newData,path);if(row.getBoolean("directory"))directory(to);else copyOrResume(child(snapshot,path),to,row);}sync(newData);}
   else verifyCurrentData(oldData,items,r.getString("profileText"));
   File started=new File(op,"rom-publication.started");if(!started.exists()){
    if(rom.exists()){if(!same||!matches(rom,r.getJSONObject("oldRom")))throw bad("A different ROM appeared at the update destination");}
    }
   expectedText(started,"ROM publication "+id+"\n");
   if(same&&rom.exists()&&matches(rom,r.getJSONObject("oldRom"))&&!matches(rom,r.getJSONObject("newRom"))){if(!rom.delete())throw bad("Cannot retire backed-up ROM image");sync(rom.getParentFile());}
   copyOrResume(after,rom,r.getJSONObject("newRom"));
   File meta=new File(newData,"profile.json"),order=new File(new File(root,"data"),"library-order.json");if(meta.exists()&&!ProfileFiles.read(meta).equals(r.getString("profileText"))&&(!same||!matches(meta,rows(items).get("profile.json"))))throw bad("Replacement profile changed");
   if(order.exists()){String now=digest(order),afterOrder=sha(utf(r.getString("orderText")));if(!now.equals(afterOrder)&&(r.isNull("orderBefore")||!now.equals(r.getString("orderBefore"))))throw bad("Library order changed during update");}else if(!r.isNull("orderBefore"))throw bad("Library order disappeared during update");
   ProfileFiles.commit(root,runtime,ProfileFiles.update("data/"+next+"/profile.json",new JSONObject(r.getString("profileText"))),ProfileFiles.update("data/library-order.json",new JSONObject(r.getString("orderText"))));
   verifyCurrentData(newData,items,r.getString("profileText"));if(!matches(rom,r.getJSONObject("newRom")))throw bad("Updated ROM verification failed");
   if(!same){writeAlias(root,runtime,r);retireOld(new File(new File(root,"Roms"),old),oldData,r.getJSONObject("oldRom"),items);}
   expectedText(new File(op,"completed.txt"),"Updated "+old+" -> "+next+"\n");File marker=new File(newData,".p2d-update-"+id);if(marker.exists()){if(!ProfileFiles.read(marker).equals(owner(root,id))||!marker.delete())throw bad("Cannot complete update ownership record");sync(newData);}
   if(!pending.delete())throw bad("Cannot complete ROM update checkpoint");sync(runtime);
  }catch(JSONException e){throw bad("Invalid update recovery metadata");}
 }
 private static String sha(byte[] data)throws IOException{try{StringBuilder s=new StringBuilder();for(byte b:java.security.MessageDigest.getInstance("SHA-256").digest(data))s.append(String.format(Locale.ROOT,"%02x",b&255));return s.toString();}catch(java.security.NoSuchAlgorithmException e){throw new IOException(e);}}
 private static boolean sameJSON(Object a,Object b)throws JSONException{
  if(a instanceof JSONObject&&b instanceof JSONObject){JSONObject x=(JSONObject)a,y=(JSONObject)b;if(x.length()!=y.length())return false;Iterator<String> it=x.keys();while(it.hasNext()){String k=it.next();if(!y.has(k)||!sameJSON(x.get(k),y.get(k)))return false;}return true;}
  if(a instanceof JSONArray&&b instanceof JSONArray){JSONArray x=(JSONArray)a,y=(JSONArray)b;if(x.length()!=y.length())return false;for(int i=0;i<x.length();i++)if(!sameJSON(x.get(i),y.get(i)))return false;return true;}
  if(a instanceof Number&&b instanceof Number)return new java.math.BigDecimal(a.toString()).compareTo(new java.math.BigDecimal(b.toString()))==0;
  return Objects.equals(a,b);
 }
 private static void verifyCurrentData(File dir,JSONArray items,String profile)throws IOException{
  try{Map<String,JSONObject> want=rows(items);for(Map.Entry<String,JSONObject> e:want.entrySet()){File f=child(dir,e.getKey());if(e.getValue().getBoolean("directory")){if(!f.isDirectory())throw bad("Saved-data directory missing");}else if(e.getKey().equals("profile.json")){if(!matches(f,e.getValue())&&!ProfileFiles.read(f).equals(profile))throw bad("Saved profile changed");}else if(!matches(f,e.getValue()))throw bad("Saved data changed: "+e.getKey());}}
  catch(JSONException e){throw bad("Invalid saved-data verification record");}
 }
 private static void retireOld(File rom,File data,JSONObject image,JSONArray items)throws IOException{
  ProfileFiles.plain(rom);if(rom.exists()&&!matches(rom,image))throw bad("Original image changed before retirement");verifyTree(data,items,true);
  if(rom.exists()&&!rom.delete())throw bad("Cannot retire original ROM");sync(rom.getParentFile());
  try{for(int i=items.length()-1;i>=0;i--){File f=child(data,items.getJSONObject(i).getString("path"));if(f.exists()&&!f.delete())throw bad("Cannot retire original data association");}if(data.exists()&&!data.delete())throw bad("Cannot retire original data directory");sync(data.getParentFile());}catch(JSONException e){throw new IOException(e);}
 }
 private static File aliases(File runtime)throws IOException{File p=new File(runtime,"rom-update-aliases");ProfileFiles.plain(p);return p;}
 private static List<JSONObject> aliasRecords(File root,File runtime)throws IOException{
  File dir=aliases(runtime);List<JSONObject> out=new ArrayList<>();if(!dir.exists())return out;File[] files=dir.listFiles();if(files==null||files.length>MAX_ENTRIES)throw bad("Update identity history is unavailable");for(File f:files){ProfileFiles.plain(f);if(f.getName().endsWith(".pending"))continue;if(!f.isFile()||!f.getName().matches("[0-9]{20}-[0-9a-f]{32}\\.json"))throw bad("Invalid update identity history");JSONObject a=ProfileFiles.object(f);try{if(a.getInt("schema")!=1||a.getLong("sequence")<1||!a.getString("root").equals(rootName(root)))throw bad("Invalid update identity root");RomRepository.binName(a.getString("old"));RomRepository.binName(a.getString("next"));ProfileFiles.name(a.getString("settingsKey"));ProfileFiles.name(a.getString("priorKey"));}catch(JSONException e){throw bad("Invalid update alias");}out.add(a);}return out;
 }
 private static long nextSequence(File root,File runtime)throws IOException{long n=0;try{for(JSONObject a:aliasRecords(root,runtime))n=Math.max(n,a.getLong("sequence"));if(n==Long.MAX_VALUE)throw bad("Update history sequence exhausted");return n+1;}catch(JSONException e){throw new IOException(e);}}
 private static void writeAlias(File root,File runtime,JSONObject r)throws IOException,JSONException{
  File dir=aliases(runtime);directory(dir);JSONObject a=new JSONObject().put("schema",1).put("root",rootName(root)).put("sequence",r.getLong("aliasSequence")).put("old",r.getString("old")).put("next",r.getString("next")).put("settingsKey",r.getString("settingsKey")).put("priorKey",r.getString("priorKey"));File file=new File(dir,String.format(Locale.ROOT,"%020d-%s.json",r.getLong("aliasSequence"),r.getString("id")));if(file.exists()){if(!sameJSON(ProfileFiles.object(file),a))throw bad("Update history changed");}else ProfileFiles.publishJournal(file,a.toString(),runtime);sync(dir);
 }
 static String resolveUpdatedName(File root,File runtime,String name)throws IOException{
  ProfileFiles.name(name);String current=name,expectedKey=null;Set<String> seen=new HashSet<>();List<JSONObject> records=aliasRecords(root,runtime);
  try{for(int step=0;step<64;step++){
   File rom=new File(new File(root,"Roms"),current),meta=new File(new File(new File(root,"data"),current),"profile.json");ProfileFiles.plain(rom);ProfileFiles.plain(meta.getParentFile());ProfileFiles.plain(meta);
   if(rom.exists()||meta.exists()){if(expectedKey==null||!meta.exists()||ProfileFiles.object(meta).optString("settingsKey",current).equals(expectedKey))return current;}
   String check=current+"\n"+expectedKey;if(!seen.add(check))throw bad("Cyclic update identity history");JSONObject best=null;for(JSONObject a:records)if(a.getString("old").equals(current)&&(expectedKey==null||a.getString("priorKey").equals(expectedKey))&&(best==null||a.getLong("sequence")>best.getLong("sequence")))best=a;
   if(best==null){if(expectedKey==null)return current;throw bad("Updated installation is unavailable");}current=best.getString("next");expectedKey=best.getString("settingsKey");
  }}catch(JSONException e){throw bad("Invalid update identity history");}throw bad("Update identity history is too deep");
 }
}
