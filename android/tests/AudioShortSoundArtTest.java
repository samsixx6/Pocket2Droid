package com.samsixx.pocket2droid;
import android.content.ContextWrapper;
import android.media.AudioTrack;
import java.lang.reflect.*;
import java.io.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;

/** Real Android output: isolated PCM must drain without another sound. */
public final class AudioShortSoundArtTest {
    static Field field(Class<?> type,String name)throws Exception {Field f=type.getDeclaredField(name);f.setAccessible(true);return f;}
    static long head(AudioTrack track){return Integer.toUnsignedLong(track.getPlaybackHeadPosition());}
    static void idle(AudioPump.Sink sink)throws Exception {
        // The old sink has no idle service. It still runs, exposing its stalled output.
        try {Method m=sink.getClass().getDeclaredMethod("idle",long.class);m.setAccessible(true);m.invoke(sink,System.nanoTime());}
        catch(NoSuchMethodException oldSink){}
    }
    static void drain(AudioPump.Sink sink,AudioTrack track,long target)throws Exception {
        long deadline=System.nanoTime()+750000000L;
        while(head(track)<target&&System.nanoTime()<deadline){idle(sink);Thread.sleep(4);}
        if(head(track)<target)throw new AssertionError("Isolated sound stalled: expected at least "+target+" played frames, got "+head(track));
    }
    static short[] tone(int frames){short[] pcm=new short[frames*2];for(int i=0;i<frames;i++)pcm[2*i]=pcm[2*i+1]=(short)(Math.sin(i*2*Math.PI*440/48000)*1500);return pcm;}
    public static void main(String[] args) {try{run();consumer(new File(args[0]));System.exit(0);}catch(Throwable e){e.printStackTrace(System.out);System.exit(1);}}
    static void run()throws Exception {
        AudioPlayer player=new AudioPlayer(new ContextWrapper(null),null,System.out::println,(id,on)->{});
        field(AudioPlayer.class,"foreground").setBoolean(player,true);
        field(AudioPlayer.class,"focused").setBoolean(player,true);
        AudioPump.Sink sink=(AudioPump.Sink)field(AudioPlayer.class,"sink").get(player);
        try {
            if(!sink.configure(48000,40))throw new AssertionError("AudioTrack unavailable");
            AudioTrack track=(AudioTrack)field(sink.getClass(),"track").get(sink);
            for(int ms:new int[]{5,10,20,40}) {
                sink.flush();long before=head(track);short[] pcm=tone(ms*48);
                int wrote=sink.write(pcm,0,pcm.length);
                if(wrote!=pcm.length)throw new AssertionError("Short PCM was not accepted");
                drain(sink,track,before+wrote/2);
                Thread.sleep(150); // An underrun followed by another isolated tap.
                before=head(track);wrote=sink.write(pcm,0,pcm.length);
                drain(sink,track,before+wrote/2);
                System.out.println("PASS isolated/repeated "+ms+" ms");
            }
            sink.flush();
            // Continuous game packets must not acquire inserted silence or disappear.
            long start=System.nanoTime();int submitted=0;short[] game=tone(480);
            while(System.nanoTime()-start<600000000L){
                int wrote=sink.write(game,0,game.length);if(wrote<0)throw new AssertionError("Game write failed");submitted+=wrote/2;
                idle(sink);Thread.sleep(10);
            }
            if(field(sink.getClass(),"submitted").getLong(sink)!=submitted)
                throw new AssertionError("Silence was inserted between continuous game packets");
            drain(sink,track,submitted);
            Thread.sleep(150);
            long after=head(track);
            if(after>submitted+track.getBufferSizeInFrames())throw new AssertionError("Unbounded generated silence");
            field(AudioPlayer.class,"enabled").setBoolean(player,false);
            if(sink.write(game,0,game.length)!=0)throw new AssertionError("Mute accepted late audio");
            sink.flush();idle(sink);Thread.sleep(80);
            if(head(track)!=0)throw new AssertionError("Idle service restarted muted audio");
            System.out.println("PASS continuous game output, bounded idle, mute and flush");
            player.setEnabled(true);
            short[] tap=tone(240);sink.write(tap,0,tap.length);
            idle(sink);Thread.sleep(25);idle(sink); // A real or padded tail may still be queued.
            player.setEnabled(false);sink.flush();idle(sink);
            if(field(sink.getClass(),"submitted").getLong(sink)!=0)
                throw new AssertionError("Muted pending tail survived flush");
            player.setEnabled(true);field(AudioPlayer.class,"focused").setBoolean(player,false);
            if(sink.write(tap,0,tap.length)!=0)throw new AssertionError("Focus loss accepted a tap");idle(sink);
            field(AudioPlayer.class,"focused").setBoolean(player,true);
            player.setForeground(false);
            if(sink.write(tap,0,tap.length)!=0)throw new AssertionError("Background accepted a tap");idle(sink);
            player.setForeground(true);sink.flush();
            int wrote=sink.write(tap,0,tap.length);drain(sink,track,wrote/2);
            System.out.println("PASS pending-tail mute/focus/background flush and fresh-tap resume");
        } finally {sink.close();}
    }
    static void consumer(File root)throws Exception {
        if(!root.isDirectory())throw new AssertionError("Owned test directory missing");
        File file=new File(root,"short-"+UUID.randomUUID()+".bin");
        AtomicBoolean allowed=new AtomicBoolean();
        AudioPlayer player=new AudioPlayer(new ContextWrapper(null),file,System.out::println,(id,value)->allowed.set(value));
        try(RandomAccessFile handle=new RandomAccessFile(file,"rw")) {
            handle.setLength(AudioProtocol.SIZE);
            ByteBuffer map=handle.getChannel().map(FileChannel.MapMode.READ_WRITE,0,AudioProtocol.SIZE).order(ByteOrder.LITTLE_ENDIAN);
            AudioProtocol.initialize(map);
            field(AudioPlayer.class,"foreground").setBoolean(player,true);
            field(AudioPlayer.class,"focused").setBoolean(player,true);
            field(AudioPlayer.class,"focusAttempted").setBoolean(player,true);
            player.start(99);
            long deadline=System.nanoTime()+1000000000L;
            while(!allowed.get()&&System.nanoTime()<deadline)Thread.sleep(2);
            if(!allowed.get())throw new AssertionError("Consumer failed to enable output");
            try(FileLock lock=handle.getChannel().lock()) {
                map.putInt(36,7);map.putInt(44,0); // The guest has already stopped normally.
                map.putInt(64,7);map.putInt(68,48000);map.putInt(72,240);map.putInt(76,0);
                map.putLong(80,System.nanoTime());map.putLong(88,0);
                short[] pcm=tone(240);for(int i=0;i<pcm.length;i++)map.putShort(2112+2*i,pcm[i]);
                map.putInt(28,960);map.putInt(20,1);
            }
            AudioPump.Sink sink=(AudioPump.Sink)field(AudioPlayer.class,"sink").get(player);
            long played=0;deadline=System.nanoTime()+1000000000L;
            while(played<240&&System.nanoTime()<deadline){
                synchronized(sink){AudioTrack track=(AudioTrack)field(sink.getClass(),"track").get(sink);if(track!=null)played=head(track);}
                Thread.sleep(4);
            }
            if(played<240)throw new AssertionError("Full consumer stalled a stopped guest's isolated sound: "+played);
            System.out.println("PASS full consumer drains isolated PCM while guest status is inactive");
        } finally {player.setForeground(false);if(!player.stop(1000))throw new AssertionError("Consumer did not stop");file.delete();}
    }
}
