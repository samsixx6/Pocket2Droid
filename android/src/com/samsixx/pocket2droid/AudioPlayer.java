package com.samsixx.pocket2droid;
import android.content.Context;
import android.media.*;
import android.os.*;
import java.io.*;
import java.nio.*;
import java.nio.channels.*;
/** One session's consumer. All playback/transport work runs outside QEMU. */
final class AudioPlayer {
    interface Status { void changed(String message); }
    interface Control { void allowed(long session,boolean allowed); default void stopped(long session) {} }
    private final Context context;private final File file;private final Status status;private final Control control;
    private final Object wake=new Object();private final TrackSink sink=new TrackSink();
    private volatile boolean foreground,enabled=true,closing,focused;
    private boolean focusAttempted;private volatile Thread thread;private long session,wakeRevision,lossRevision,focusGeneration,focusCallbacks;
    private AudioManager manager;private AudioFocusRequest focusRequest;
    private AudioManager.OnAudioFocusChangeListener focusListener;
    AudioPlayer(Context context,File file,Status status,Control control){
        this.context=context;this.file=file;this.status=status;this.control=control;
        focusListener=listener(0);
    }
    private AudioManager.OnAudioFocusChangeListener listener(long request){return change->{
        boolean loss;
        synchronized(wake){if(request!=focusGeneration||closing)return;
            focused=change==AudioManager.AUDIOFOCUS_GAIN;loss=!focused;
            focusCallbacks++;
            if(loss){lossRevision++;control.allowed(session,false);}wakeRevision++;wake.notifyAll();}
    };}
    void setForeground(boolean v){synchronized(wake){foreground=v;changed();}}
    void setEnabled(boolean v){synchronized(wake){enabled=v;changed();}}
    private void changed(){boolean loss=!foreground||!enabled;if(loss){lossRevision++;control.allowed(session,false);}wakeRevision++;wake.notifyAll();}
    synchronized void start(long id){if(thread!=null)throw new IllegalStateException("Audio consumer already started");session=id;thread=new Thread(()->{try{run();}finally{thread=null;control.stopped(session);}},"p2d-audio");thread.start();}
    boolean alive(){Thread t=thread;return t!=null&&t.isAlive();}
    boolean stop(long timeoutMs){synchronized(wake){closing=true;control.allowed(session,false);wake.notifyAll();}Thread t=thread;if(t!=null&&t!=Thread.currentThread())try{t.join(timeoutMs);}catch(InterruptedException e){Thread.currentThread().interrupt();}return !alive();}
    private void await(long ms,long revision)throws InterruptedException{synchronized(wake){if(!closing&&wakeRevision==revision)wake.wait(ms);}}
    private void requestFocus(){
        long request,callbacks;
        synchronized(wake){request=++focusGeneration;callbacks=focusCallbacks;focusListener=listener(request);focused=false;}
        manager=(AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
        focusAttempted=true;
        if(manager==null){focused=false;return;}
        int result;
        if(Build.VERSION.SDK_INT>=26){focusRequest=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(focusListener,new Handler(Looper.getMainLooper())).build();result=manager.requestAudioFocus(focusRequest);}
        else result=manager.requestAudioFocus(focusListener,AudioManager.STREAM_MUSIC,AudioManager.AUDIOFOCUS_GAIN);
        focusResult(request,callbacks,result);
        if(!focused)status.changed("Sound paused: audio focus unavailable");
    }
    private void focusResult(long request,long callbacks,int result){synchronized(wake){
        if(request==focusGeneration&&callbacks==focusCallbacks&&!closing)
            focused=result==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
    }}
    private void abandonFocus(){synchronized(wake){focused=false;focusGeneration++;}if(manager!=null&&focusAttempted){if(Build.VERSION.SDK_INT>=26&&focusRequest!=null)manager.abandonAudioFocusRequest(focusRequest);else manager.abandonAudioFocus(focusListener);}focusRequest=null;focusAttempted=false;}
    private void run(){
        AudioPump pump=new AudioPump(sink,status::changed);boolean allowed=false,flushPending=false;int previousEpoch=0,previousStatus=-1;long observedLoss=-1;
        try(RandomAccessFile handle=new RandomAccessFile(file,"rw");FileChannel channel=handle.getChannel()) {
            if(handle.length()!=AudioProtocol.SIZE)throw new IOException("Wrong audio buffer size");
            ByteBuffer map=channel.map(FileChannel.MapMode.READ_WRITE,0,AudioProtocol.SIZE).order(ByteOrder.LITTLE_ENDIAN);
            short[] pcm=new short[4096];AudioProtocol.Packet packet=new AudioProtocol.Packet();
            while(!closing) {
                long revision,loss; synchronized(wake){revision=wakeRevision;loss=lossRevision;if(loss!=observedLoss)control.allowed(session,false);}
                if(loss!=observedLoss){observedLoss=loss;allowed=false;pump.discard();flushPending=true;}
                boolean wanted=foreground&&enabled&&!pump.failed();
                if(!wanted){if(allowed){allowed=false;control.allowed(session,false);}pump.active(false);abandonFocus();await(0,revision);continue;}
                if(!focusAttempted)requestFocus();
                if(!focused){if(allowed){allowed=false;control.allowed(session,false);}pump.active(false);await(0,revision);continue;}
                if(flushPending){
                    try(FileLock lock=channel.tryLock()) {if(lock!=null){AudioProtocol.validate(map,System.nanoTime());AudioProtocol.flush(map);flushPending=false;}}
                    catch(OverlappingFileLockException contended){/* bounded retry */}
                    if(flushPending){await(4,revision);continue;}
                }
                if(!allowed){
                    synchronized(wake){if(closing||!foreground||!enabled||!focused||lossRevision!=observedLoss)continue;
                        allowed=true;control.allowed(session,true);}
                    pump.active(true);
                }
                try(FileLock lock=channel.tryLock()){
                    if(lock!=null){AudioProtocol.validate(map,System.nanoTime());
                        int epoch=map.getInt(36);
                        if(epoch!=previousEpoch){pump.discard();previousEpoch=epoch;}}}
                catch(OverlappingFileLockException contended){/* bounded retry */}
                boolean progress=pump.step(System.nanoTime());
                if(pump.pending()){await(progress?1:4,revision);continue;}
                int n=0,epoch=previousEpoch,outputStatus=previousStatus;
                try(FileLock lock=channel.tryLock()){
                    if(lock!=null){n=AudioProtocol.read(map,pcm,packet,System.nanoTime());epoch=map.getInt(36);outputStatus=map.getInt(44);}
                }catch(OverlappingFileLockException contended){/* bounded retry */}
                if(epoch!=previousEpoch){pump.discard();previousEpoch=epoch;}
                if(outputStatus!=previousStatus){previousStatus=outputStatus;if(outputStatus==2){pump.discard();status.changed("Sound: unsupported guest IIS format");}else if(outputStatus==3)throw new IOException("Native sound output unavailable");}
                if(n>0){pump.accept(packet,pcm);statusIfRateChanged(packet.rate);}
                else {sink.idle(System.nanoTime());await(outputStatus==0?20:4,revision);}
            }
        }catch(Exception error){status.changed("Sound disabled: "+error.getMessage());}
        finally{control.allowed(session,false);pump.close();abandonFocus();}
    }
    private int lastRate;
    private void statusIfRateChanged(int rate){if(lastRate!=rate){lastRate=rate;status.changed("Sound playing ("+rate+" Hz)");}}
    private final class TrackSink implements AudioPump.Sink {
        private AudioTrack track;private int configuredRate;
        private short[] silence;
        private long submitted,lastWrite,lastHead,headWrap,lastProgress;
        public synchronized boolean configure(int rate,int bufferMs){
            if(track!=null&&configuredRate==rate)return true;
            close();
            try{
                int min=AudioTrack.getMinBufferSize(rate,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT);
                if(min<=0||min>(long)rate*4/5)return false;
                int bytes=Math.max(min,(rate*4*bufferMs+999)/1000);bytes=(bytes+3)&~3;
                track=new AudioTrack(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
                    new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setSampleRate(rate).build(),
                    bytes,AudioTrack.MODE_STREAM,AudioManager.AUDIO_SESSION_ID_GENERATE);
                if(track.getState()!=AudioTrack.STATE_INITIALIZED){close();return false;}
                silence=new short[bytes/2];
                configuredRate=rate;return true;
            }catch(RuntimeException unavailable){close();return false;}
        }
        public synchronized int write(short[] pcm,int offset,int count){
            if(!foreground||!enabled||!focused||closing)return 0;
            if(track==null)return AudioTrack.ERROR_INVALID_OPERATION;
            try{if(track.getPlayState()!=AudioTrack.PLAYSTATE_PLAYING)track.play();
                int wrote=track.write(pcm,offset,count,AudioTrack.WRITE_NON_BLOCKING);
                if(wrote>0){submitted+=wrote/2;lastWrite=System.nanoTime();}
                return wrote;}
            catch(RuntimeException error){return AudioTrack.ERROR_INVALID_OPERATION;}
        }
        synchronized void idle(long now){
            if(track==null||!foreground||!enabled||!focused||closing||lastWrite==0)return;
            try{
                long raw=Integer.toUnsignedLong(track.getPlaybackHeadPosition());
                if(raw<(lastHead&0xffffffffL))headWrap+=1L<<32;
                long head=headWrap+raw;
                if(head!=lastHead){lastHead=head;lastProgress=now;}
                if(lastProgress==0)lastProgress=now;
                // API24-30 (and devices with a larger minimum threshold) need a
                // full buffer after starvation. Only pad a stalled, idle burst;
                // regular game packets continue unchanged, with no silence loop.
                if(now-lastWrite<20000000L||now-lastProgress<20000000L||head>=submitted)return;
                long missing=(long)track.getBufferSizeInFrames()-(submitted-head);
                if(missing<=0)return;
                int count=(int)Math.min(missing*2,silence.length);
                int wrote=track.write(silence,0,count,AudioTrack.WRITE_NON_BLOCKING);
                if(wrote>0)submitted+=wrote/2;
            }catch(RuntimeException ignored){/* normal write handles track failure */}
        }
        private void resetProgress(){submitted=lastWrite=lastHead=headWrap=lastProgress=0;}
        public synchronized void flush(){if(track!=null)try{track.pause();track.flush();}catch(RuntimeException ignored){}resetProgress();}
        public synchronized void close(){if(track!=null){try{track.pause();track.flush();}catch(RuntimeException ignored){}track.release();track=null;}configuredRate=0;silence=null;resetProgress();}
    }
}
