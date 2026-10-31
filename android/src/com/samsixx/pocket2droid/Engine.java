package com.samsixx.pocket2droid;
import android.content.Context;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;

final class Engine {
    private static final Object ownership = new Object();
    private static Engine owner;
    private volatile boolean importing, disposed;
    private final Context context;
    final StorageLayout layout;
    final File root, rom, cab, storage, frames;
    // Successive Engines share the runtime frame file; retired readers can overlap.
    static final Object frameAccess=new Object();
    final String executable;
    interface Status { void changed(String message); }
    interface Completion { void finished(boolean success); }
    private final Status status;
    Status audioStatus;
    private final AudioSession audioSession = new AudioSession();
    private volatile AudioPlayer audioPlayer;
    private volatile boolean soundEnabled = true;
    private volatile long audioToken;
    private boolean nativeSound;
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor();
    private static final class Press {
        final long began = System.nanoTime();
        boolean releaseRequested;
    }
    private final Map<String, Press> held = new HashMap<>();
    private volatile java.lang.Process process;
    private volatile boolean desiredRunning;
    volatile boolean starting;
    private volatile boolean expectedExit;
    private boolean pointerDown;
    volatile boolean frameReady;
    volatile long generation;
    private LocalSocket socket;
    private BufferedReader input;
    private OutputStream output;
    private volatile RandomAccessFile storageHandle;
    private volatile RandomAccessFile ramHandle;
    private File retainedRam;
    private NetworkSession networkSession;
    private File networkSetupFile;
    private GlesSetup.Bundle glesSetup;
    private int requestId;

    Engine(Context context, Status sink) {
        this(context, sink, StorageLayout.forContext(context));
    }
    Engine(Context context, Status sink, StorageLayout paths) {
        this.context = context; layout = paths;
        root = layout.root; rom = layout.rom; cab = layout.cab;
        storage = layout.storage; frames = layout.frames;
        executable = new File(context.getApplicationInfo().nativeLibraryDir, "libp2dengine.so").getPath();
        status = sink;
        audioStatus = sink;
    }
    boolean alive() {
        java.lang.Process p = process;
        if (p == null) return false;
        try { p.exitValue(); return false; } catch (IllegalThreadStateException running) { return true; }
    }
    static String digest(File file) throws Exception {
        return StorageLayout.digest(file);
    }
    private boolean audioAlive() { AudioPlayer p=audioPlayer;return p!=null&&p.alive(); }
    boolean busy() { synchronized (ownership) { return starting || importing || alive() || audioAlive() || storageHandle != null || ramHandle != null || (owner != null && owner != this); } }
    boolean beginImport() {
        synchronized (ownership) {
            if (disposed || starting || importing || alive() || audioAlive() || storageHandle != null || ramHandle != null || (owner != null && owner != this)) return false;
            if(!SessionGuard.claim(this,layout.profile==null?null:layout.profile.filename))return false;
            importing = true; owner = this; return true;
        }
    }
    void endImport() { synchronized (ownership) { importing = false; releaseOwner(); } }
    private void releaseOwner() {
        synchronized (ownership) {
            if (owner == this && !starting && !importing && !alive() && !audioAlive() && storageHandle == null && ramHandle == null) { owner = null;SessionGuard.release(this); }
        }
    }
    ProcessBuilder processBuilder() throws IOException {
        RomSettings.GuestSize size=layout.profile==null?new RomSettings.GuestSize(240,320):layout.profile.settings.startupSize(layout.profile.filename);
        String machine = "p2d-devemu,storage=" + option(storage) + ",frame-file=" + option(frames) +
                         ",screen-width="+size.width+",screen-height="+size.height+
                         ",audio-file=" + option(layout.audio) +
                         ",shared-folder=" + option(layout.sdcard) + ",storage-lock=" + option(new File(layout.runtime, "engine-storage.lock")) +
                         ",parent-pid=" + android.os.Process.myPid();
        if(retainedRam!=null){
            if(ramHandle==null)throw new IOException("WM5 retained RAM handle unavailable");
            try{
                android.system.StructStat stat=android.system.Os.fstat(ramHandle.getFD());
                machine+=",ram-path="+option(retainedRam)+",ram-identity="+
                    Long.toHexString(stat.st_dev)+":"+Long.toHexString(stat.st_ino);
            }catch(android.system.ErrnoException error){throw new IOException("Cannot identify WM5 RAM backing file",error);}
        }
        boolean internet=layout.profile!=null&&layout.profile.settings.internetEnabled;
        if(internet){machine+=",network=on";if(glesSetup==null)machine=NetworkSetup.appendMachineOption(machine,networkSetupFile);}
        machine=GlesSetup.appendMachineOptions(machine,glesSetup,new File(context.getApplicationInfo().nativeLibraryDir,"libp2d_gles.so"));
        List<String> args=new ArrayList<>(Arrays.asList(executable, "-machine", machine,
            "-m", "128M", "-bios", rom.getPath(), "-accel", "tcg,thread=single",
            "-display", "none", "-monitor", "none", "-serial", "none", "-S",
            "-qmp", "unix:" + option(layout.control) + ",server=on,wait=off"));
        NetworkConfig.appendArguments(args,internet,internet?layout.profile.settings.networkMac:null);
        ProcessBuilder builder=new ProcessBuilder(args);
        configureGlesBackend(builder,layout.profile==null?null:layout.profile.settings);
        return builder;
    }
    static void configureGlesBackend(ProcessBuilder builder,RomSettings settings){
        Map<String,String> environment=builder.environment();
        // Override inherited or legacy CPU/Off selections; the native selector owns fallback.
        environment.put("P2D_GLES_BACKEND","gpu");
    }
    private static String option(File file){return file.getPath().replace(",",",,");}
    void prepareFrames()throws IOException {
        RomSettings.GuestSize size=layout.profile==null?new RomSettings.GuestSize(240,320):layout.profile.settings.startupSize(layout.profile.filename);
        synchronized(frameAccess){try(RandomAccessFile f=new RandomAccessFile(frames,"rw")){FrameProtocol.initialize(f,size.width,size.height);}}
    }
    static String activeProfileFilename(){return SessionGuard.filename();}
    void start() {
        synchronized (ownership) {
            if (disposed || starting) return;
            if (importing || (owner != null && owner != this)) { status.changed("Wait for storage work or the previous session to finish."); return; }
            if(!SessionGuard.claim(this,layout.profile==null?null:layout.profile.filename)){status.changed("Wait for library storage work to finish.");return;}
            owner = this; starting = true; desiredRunning = true;
            audioSession.foreground(true);
        }
        worker.execute(() -> {
            try {
                if (alive()) { applyRunning(desiredRunning); return; }
                if (!cleanup()) throw new IOException("Previous engine is still exiting");
                if (!StorageAccess.granted(context)) throw new IOException("Allow all-files storage access, then Start. Guest files stay in pocket2droid.");
                status.changed("Preparing pocket2droid folders and checking ROM...");
                layout.prepare(); StorageLayout.verifyRom(rom);
                networkSetupFile=null;
                try{networkSetupFile=NetworkSetup.prepare(context,layout.profile==null?null:layout.profile.settings);}
                catch(IOException unavailable){status.changed("Automatic network setup unavailable. In Windows Mobile Network Cards, select The Internet.");}
                glesSetup=GlesSetup.prepare(context,layout.profile==null?"legacy":layout.profile.settingsKey,layout.profile==null?null:layout.profile.settings,networkSetupFile);
                // Open before QEMU can dirty pages, retaining Linux's writeback-error cursor.
                storageHandle = new RandomAccessFile(storage, "rw");
                retainedRam=GuestRam.prepare(rom,storage.getParentFile(),layout.runtime);
                if(retainedRam!=null)ramHandle=new RandomAccessFile(retainedRam,"rw");
                prepareFrames();
                boolean audioReady=false;
                try (RandomAccessFile f = new RandomAccessFile(layout.audio,"rw")) {
                    f.setLength(AudioProtocol.SIZE);
                    java.nio.ByteBuffer buffer=f.getChannel().map(java.nio.channels.FileChannel.MapMode.READ_WRITE,0,AudioProtocol.SIZE);
                    AudioProtocol.initialize(buffer);audioReady=true;
                } catch(Exception unavailable) {audioStatus.changed("Sound unavailable: "+unavailable.getMessage());}
                File control = layout.control;
                if (control.exists() && !control.delete()) throw new IOException("Cannot remove stale control socket");
                ProcessBuilder builder = processBuilder();
                builder.environment().remove("P2D_SLIRP_RESOLV_CONF");
                if(layout.profile!=null&&layout.profile.settings.internetEnabled){
                    networkSession=new NetworkSession(context,new File(context.getFilesDir(),"network"),worker,status::changed);
                    networkSession.start();
                    builder.environment().put("P2D_SLIRP_RESOLV_CONF",networkSession.resolverFile().getAbsolutePath());
                }
                builder.redirectErrorStream(true);
                builder.environment().remove("LD_PRELOAD"); builder.environment().remove("LD_LIBRARY_PATH");
                File temporary = layout.temporary;
                if (!temporary.isDirectory() && !temporary.mkdir()) throw new IOException("Cannot create private engine temporary directory");
                builder.environment().put("TMPDIR", temporary.getPath());
                builder.environment().put("LD_LIBRARY_PATH", new File(executable).getParent());
                process = builder.start();
                expectedExit = false;
                java.lang.Process owner = process;
                final NetworkSession processNetwork=networkSession;
                new Thread(() -> {
                    try {
                        int code = owner.waitFor();
                        if(processNetwork!=null)processNetwork.close();
                        try {
                            worker.execute(() -> {
                                if (process == owner && !expectedExit) {
                                    frameReady = false;
                                    stopAudio();
                                    status.changed("Engine exited (code " + code + "); see engine.log");
                                }
                            });
                        } catch (RejectedExecutionException disposed) { /* Activity already closed. */ }
                    } catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
                }, "engine-exit").start();
                new Thread(() -> {
                    try (InputStream in = owner.getInputStream(); OutputStream log = new FileOutputStream(layout.log, true)) {
                        byte[] bytes = new byte[4096]; int n;
                        while ((n = in.read(bytes)) != -1) log.write(bytes, 0, n);
                    } catch (IOException ignored) { }
                }, "engine-log").start();
                long deadline = System.nanoTime() + 10_000_000_000L;
                while (true) {
                    if (!alive()) throw new IOException("Engine exited (code " + process.exitValue() + "); see pocket2droid/data/engine.log");
                    LocalSocket candidate = new LocalSocket();
                    try {
                        candidate.connect(new LocalSocketAddress(control.getPath(), LocalSocketAddress.Namespace.FILESYSTEM));
                        candidate.setSoTimeout(5000); socket = candidate; break;
                    } catch (IOException notReady) {
                        candidate.close();
                        if (System.nanoTime() > deadline) throw new IOException("Engine control startup timed out", notReady);
                        Thread.sleep(50);
                    }
                }
                input = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
                output = socket.getOutputStream();
                String greeting = input.readLine();
                if (greeting == null || !new JSONObject(greeting).has("QMP")) throw new IOException("Invalid engine greeting");
                command("qmp_capabilities", null);
                frameReady = true; generation++;
                nativeSound=false;
                if(audioReady){
                    audioToken=audioSession.begin();
                    AudioPlayer player=new AudioPlayer(context,layout.audio,audioStatus::changed,audioControl());
                    audioPlayer=player;player.setEnabled(soundEnabled);player.setForeground(desiredRunning);player.start(audioToken);
                }
                applyRunning(desiredRunning);
            } catch (Exception e) { failure(e); }
            finally { starting = false; releaseOwner(); }
        });
    }
    private JSONObject command(String name, JSONObject arguments) throws Exception {
        if (output == null) throw new IOException("Engine control is not connected");
        int id = ++requestId;
        JSONObject call = new JSONObject().put("execute", name).put("id", id);
        if (arguments != null) call.put("arguments", arguments);
        output.write((call.toString() + "\n").getBytes(StandardCharsets.UTF_8)); output.flush();
        while (true) {
            String line = input.readLine();
            if (line == null) throw new EOFException("Engine control closed");
            JSONObject reply = new JSONObject(line);
            if (reply.optInt("id", -1) != id) continue;
            if (reply.has("error")) throw new IOException(reply.getJSONObject("error").optString("desc"));
            return reply;
        }
    }
    void running(boolean value) {
        desiredRunning = value;
        audioSession.foreground(value);
        AudioPlayer player=audioPlayer;if(player!=null)player.setForeground(value);
        worker.execute(() -> {
            if (!alive() || output == null) return;
            try {
                applyRunning(value);
            } catch (Exception e) { failure(e); }
        });
    }
    void sound(boolean value) {
        soundEnabled=value;audioSession.sound(value);
        AudioPlayer player=audioPlayer;if(player!=null)player.setEnabled(value);
        audioStatus.changed(value?"Sound enabled":"Sound muted");
        queueSound();
    }
    private void audioAllowed(long token,boolean value) {
        if(!audioSession.current(token))return;
        audioSession.allow(token,value);queueSound();
    }
    private void audioStopped(long token){
        if(token==audioToken)releaseOwner();
    }
    private AudioPlayer.Control audioControl(){return new AudioPlayer.Control(){
        public void allowed(long token,boolean value){audioAllowed(token,value);}
        public void stopped(long token){audioStopped(token);}
    };}
    private void queueSound(){
        try{worker.execute(this::applySound);}catch(RejectedExecutionException closed){/* closing activity */}
    }
    private void applySound(){
        if(!alive()||output==null)return;
        boolean enable=audioSession.nativeEnabled();
        if(enable==nativeSound)return;
        try{
            command("qom-set",new JSONObject().put("path","/machine/p2d-media").put("property","output-enabled").put("value",enable));
            nativeSound=enable;
            if(enable){JSONObject reply=command("qom-get",new JSONObject().put("path","/machine/p2d-media").put("property","output-status"));
                if("output-failed".equals(reply.optString("return")))throw new IOException("Native audio buffer unavailable");}
        }catch(Exception error){audioSession.allow(audioToken,false);AudioPlayer p=audioPlayer;if(p!=null)p.setEnabled(false);audioStatus.changed("Sound disabled: "+error.getMessage());}
    }
    private boolean stopAudio(){
        audioSession.end(audioToken);
        AudioPlayer p=audioPlayer;
        if(p!=null&&!p.stop(1000)){audioStatus.changed("Sound shutdown pending; restart blocked");return false;}
        audioPlayer=null;nativeSound=false;return true;
    }
    private void syncStorage() throws IOException {
        if (storageHandle == null || storageHandle.length() != 32L * 1024 * 1024)
            throw new IOException("Persistent storage handle unavailable or size changed");
        StorageSync.flush(storageHandle.getFD());
        if(retainedRam!=null){
            if(ramHandle==null)throw new IOException("WM5 retained RAM handle unavailable");
            syncRam();
        }
    }
    private void syncRam()throws IOException{
        if(ramHandle.length()!=GuestRam.SIZE)throw new IOException("WM5 retained RAM size changed");
        StorageSync.flush(ramHandle.getFD());
    }
    private void applyRunning(boolean run) throws Exception {
        if (!run) releaseKeys();
        command(run ? "cont" : "stop", null);
        if (!run) syncStorage();
        applySound();
        status.changed(run ? "Running TCG" : "Paused; storage flushed");
    }
    private void keyNow(String key, boolean down) throws Exception {
        JSONArray events = new JSONArray().put(new JSONObject().put("type", "key").put("data",
            new JSONObject().put("down", down).put("key", new JSONObject().put("type", "qcode").put("data", key))));
        command("input-send-event", new JSONObject().put("events", events));
    }
    private void releaseKeys() throws Exception {
        for (String key : new ArrayList<>(held.keySet())) keyNow(key, false);
        held.clear();
        if (pointerDown) {
            command("input-send-event", new JSONObject().put("events", new JSONArray().put(
                new JSONObject().put("type", "btn").put("data", new JSONObject().put("button", "left").put("down", false)))));
            pointerDown = false;
        }
    }
    void key(String key, boolean down) {
        if (GuestKey.fromQcode(key) == null) return;
        worker.execute(() -> {
            if (!alive() || !desiredRunning || output == null) return;
            try {
                if (down) {
                    Press previous = held.get(key);
                    if (previous != null) {
                        // Coalesce re-presses inside the minimum hold. Do not
                        // shorten it or let a pending release end a renewed hold.
                        previous.releaseRequested = false; return;
                    }
                    keyNow(key, true);
                    held.put(key, new Press());
                } else {
                    Press press = held.get(key);
                    if (press == null || press.releaseRequested) return;
                    press.releaseRequested = true;
                    // WM games poll key state; an immediate make/break can both
                    // finish before the next game update. Do not block the worker.
                    long delay = Math.max(0, 100_000_000L - (System.nanoTime() - press.began));
                    worker.schedule(() -> {
                        if (held.get(key) != press || !press.releaseRequested) return;
                        try {
                            // running(false) changes intent before its queued pause.
                            // Preserve the break even in that interval; otherwise
                            // pause could lose the key's ownership without releasing it.
                            if (alive() && output != null) keyNow(key, false);
                            held.remove(key);
                        } catch (Exception e) { failure(e); }
                    }, delay, TimeUnit.NANOSECONDS);
                }
            } catch (Exception e) { failure(e); }
        });
    }
    void touch(int x, int y, int width, int height, boolean down) {
        worker.execute(() -> {
            if (!alive() || !desiredRunning || output == null) return;
            try {
                JSONArray events = new JSONArray();
                events.put(new JSONObject().put("type", "abs").put("data", new JSONObject().put("axis", "x").put("value", x * 32767 / Math.max(1, width - 1))));
                events.put(new JSONObject().put("type", "abs").put("data", new JSONObject().put("axis", "y").put("value", y * 32767 / Math.max(1, height - 1))));
                events.put(new JSONObject().put("type", "btn").put("data", new JSONObject().put("button", "left").put("down", down)));
                command("input-send-event", new JSONObject().put("events", events));
                pointerDown = down;
            } catch (Exception e) { failure(e); }
        });
    }
    private void failure(Exception e) {
        frameReady = false;
        status.changed("Engine error: " + e.getMessage());
        cleanup();
    }
    private boolean cleanup() {
        frameReady = false; held.clear(); pointerDown = false; expectedExit = true;
        boolean audioStopped=stopAudio();
        if (socket != null) try { socket.close(); } catch (IOException ignored) { }
        socket = null; input = null; output = null;
        if (alive()) {
            process.destroy(); // SIGTERM: QEMU's orderly shutdown handler.
            long deadline = System.nanoTime() + 5_000_000_000L;
            while (alive() && System.nanoTime() < deadline) {
                try { Thread.sleep(10); } catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
            }
            if (alive()) { status.changed("Engine shutdown pending; restart blocked to protect storage"); return false; }
        }
        process = null;
        if(networkSession!=null){networkSession.close();networkSession=null;}
        if (storageHandle != null || ramHandle != null) {
            try {
                // Retry partial close failures without requiring a handle that
                // has already closed. Never release ownership while either lives.
                if(storageHandle!=null){
                    if(storageHandle.length()!=StorageLayout.STORAGE_SIZE)throw new IOException("Persistent storage size changed");
                    StorageSync.flush(storageHandle.getFD());
                }
                if(ramHandle!=null){syncRam();ramHandle.close();ramHandle=null;}
                if(storageHandle!=null){storageHandle.close();storageHandle=null;}
                retainedRam=null;
            }
            catch (IOException error) { status.changed("Storage sync/close error: " + error.getMessage()); return false; }
        }
        releaseOwner();
        return audioStopped;
    }
    void close() { close(null); }
    void close(Completion completion) {
        desiredRunning = false;
        audioSession.foreground(false);
        AudioPlayer player=audioPlayer;if(player!=null)player.setForeground(false);
        worker.execute(() -> {
            expectedExit = true;
            boolean synced = false, failed = false;
            try {
                if (alive() && output != null) {
                    releaseKeys(); command("stop", null); syncStorage();
                    synced = true; command("quit", null);
                } else if (storageHandle != null || ramHandle != null) {
                    if(storageHandle!=null)syncStorage();else syncRam();
                    synced = true;
                }
            } catch (Exception error) { failed = true; status.changed("Exit/sync error: " + error.getMessage()); }
            finally { boolean success=cleanup()&&!failed;if(success)status.changed(synced?"Stopped; internal storage flushed":"Stopped");if(completion!=null)completion.finished(success); }
        });
    }
    void dispose() { disposed = true; close(); worker.shutdown(); }
}
