package com.samsixx.pocket2droid;
import android.app.*;
import android.content.*;
import android.graphics.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

abstract class LibraryUi extends Activity {
    static final int TOOLBAR=0xff19212b,TOOLBAR_TEXT=0xffedf3fa,ACCENT=0xff54a7ff;
    int BACKGROUND,PANEL,TEXT,MUTED,DIVIDER;
    void theme(){boolean dark=(getResources().getConfiguration().uiMode&android.content.res.Configuration.UI_MODE_NIGHT_MASK)==android.content.res.Configuration.UI_MODE_NIGHT_YES;
        setTheme(dark?android.R.style.Theme_Material_NoActionBar:android.R.style.Theme_Material_Light_NoActionBar);
        BACKGROUND=dark?0xff10151b:0xfffafafa;PANEL=dark?0xff19212b:0xfffafafa;TEXT=dark?0xffedf3fa:0xff202124;MUTED=dark?0xff9aa9bb:0xff626970;DIVIDER=dark?0xff343d48:0xffdadce0;
    }
    int navigationBackground(){return InsetBackdrop.navigation(Build.VERSION.SDK_INT,TEXT==0xff202124,BACKGROUND,TOOLBAR);}
    @Override protected void onCreate(Bundle state){theme();super.onCreate(state);getWindow().setStatusBarColor(TOOLBAR);getWindow().setNavigationBarColor(navigationBackground());if(Build.VERSION.SDK_INT>=29){getWindow().setStatusBarContrastEnforced(false);getWindow().setNavigationBarContrastEnforced(false);}getWindow().getDecorView().setBackgroundColor(BACKGROUND);getWindow().getDecorView().setSystemUiVisibility(TEXT==0xff202124&&Build.VERSION.SDK_INT>=26?View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR:0);}
    void statusText(String value){if(status!=null){status.setText(value);status.setVisibility(value==null||value.isEmpty()?View.GONE:View.VISIBLE);}}
    final Handler ui=new Handler(Looper.getMainLooper());
    final ExecutorService io=Executors.newSingleThreadExecutor();
    private boolean destroyed;
    private View contentRoot;
    boolean working;
    TextView status;
    interface Job<T>{T run()throws Exception;}
    interface Result<T>{void accept(T value)throws Exception;}
    int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    android.content.SharedPreferences prefs(){return getSharedPreferences("settings",MODE_PRIVATE);}
    int controlsTransparency(){return Math.max(0,Math.min(100,prefs().getInt("controls_transparency",50)));}
    void post(Runnable r){ui.post(()->{if(!destroyed&&!isFinishing())r.run();});}
    void error(String text){post(()->new AlertDialog.Builder(this).setTitle("Pocket2Droid").setMessage(text).setPositiveButton("OK",null).show());}
    <T>void task(Job<T> job,Result<T> result){if(working)return;working=true;statusText("Working…");io.execute(()->{try{T value=job.run();post(()->{working=false;statusText("");try{result.accept(value);}catch(Exception e){error(e.getMessage());}});}catch(Exception e){post(()->{working=false;statusText("");error(e.getMessage());});}});}
    RomRepository repository()throws IOException {
        if(!StorageAccess.granted(this))throw new IOException("Allow storage access to use pocket2droid folders");
        StorageLayout paths=StorageLayout.forContext(this);if(paths.root==null)throw new IOException("Shared storage is unavailable");
        File card=new File(paths.root,"storagecard");
        return new RomRepository(paths.root,paths.runtime,card);
    }
    boolean access(){if(StorageAccess.granted(this))return true;try{StorageAccess.request(this);}catch(Exception e){error("Enable storage access in Android settings, then return.");}return false;}
    LinearLayout column(){LinearLayout v=new LinearLayout(this);v.setOrientation(LinearLayout.VERTICAL);v.setBackgroundColor(BACKGROUND);return v;}
    boolean immersivePresentation(){return false;}
    boolean guestPresentation(){return false;}
    void refreshInsets(){if(contentRoot!=null)contentRoot.requestApplyInsets();}
    void install(View root){contentRoot=root;InsetBackdrop backdrop=new InsetBackdrop(guestPresentation()?Color.BLACK:BACKGROUND,guestPresentation()?Color.BLACK:TOOLBAR,guestPresentation()?Color.BLACK:navigationBackground());root.setBackground(backdrop);root.setOnApplyWindowInsetsListener((view,insets)->{
        int left=insets.getSystemWindowInsetLeft(),top=insets.getSystemWindowInsetTop(),right=insets.getSystemWindowInsetRight(),bottom=insets.getSystemWindowInsetBottom();
        if(immersivePresentation()){
            left=top=right=bottom=0;
            if(Build.VERSION.SDK_INT>=28){DisplayCutout cutout=insets.getDisplayCutout();if(cutout!=null){left=cutout.getSafeInsetLeft();top=cutout.getSafeInsetTop();right=cutout.getSafeInsetRight();bottom=cutout.getSafeInsetBottom();}}
        }
        view.setPadding(left,top,right,bottom);backdrop.insets(top,bottom);return insets;
    });setContentView(root);root.requestApplyInsets();}
    TextView text(String s,int size,int color){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);return t;}
    void glesBadge(LinearLayout parent){
        boolean light=TEXT==0xff202124;
        TextView badge=text("GLES",11,light?0xff007785:0xffb9f1f5);
        badge.setTypeface(null,Typeface.BOLD);badge.setPadding(dp(10),dp(3),dp(10),dp(3));
        badge.setContentDescription("GLES support built into ROM");
        android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();
        background.setColor(light?0xffe0f7fa:0xff173b42);background.setCornerRadius(dp(14));background.setStroke(dp(1),light?0xff9ee6ef:0xff377a83);badge.setBackground(background);
        LinearLayout.LayoutParams margin=new LinearLayout.LayoutParams(-2,-2);margin.topMargin=dp(6);parent.addView(badge,margin);
    }
    Button action(String s,Runnable r){Button b=new Button(this);b.setText(s);b.setTextColor(TEXT);b.setTextSize(14);b.setAllCaps(false);b.setMinWidth(0);b.setMinimumWidth(0);b.setOnClickListener(v->{if(!working)r.run();});return b;}
    android.graphics.drawable.Drawable toolbarBackground(){android.graphics.drawable.GradientDrawable mask=new android.graphics.drawable.GradientDrawable();mask.setColor(Color.WHITE);mask.setCornerRadius(dp(24));return new android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(0x3354a7ff),null,mask);}
    ImageButton iconButton(ToolbarIcon.Symbol symbol,String description,Runnable action){ImageButton b=new ImageButton(this);b.setImageDrawable(new ToolbarIcon(symbol,TOOLBAR_TEXT,dp(24)));b.setScaleType(ImageView.ScaleType.CENTER);b.setBackground(toolbarBackground());b.setContentDescription(description);b.setPadding(0,0,0,0);b.setOnClickListener(v->{if(!working)action.run();});return b;}
    LinearLayout bar(String title,Runnable right,String rightText){LinearLayout b=new LinearLayout(this);b.setGravity(Gravity.CENTER_VERTICAL);b.setPadding(dp(4),0,dp(8),0);b.setBackgroundColor(TOOLBAR);b.addView(iconButton(ToolbarIcon.Symbol.BACK,"Back",this::finish),new LinearLayout.LayoutParams(dp(48),dp(56)));TextView name=text(title,19,TOOLBAR_TEXT);name.setSingleLine(true);name.setEllipsize(android.text.TextUtils.TruncateAt.END);b.addView(name,new LinearLayout.LayoutParams(0,dp(56),1));name.setGravity(Gravity.CENTER_VERTICAL);if(right!=null){Button control=action(rightText,right);control.setTextColor(TOOLBAR_TEXT);control.setBackground(toolbarBackground());b.addView(control,new LinearLayout.LayoutParams(dp(64),dp(48)));}return b;}
    void section(LinearLayout parent,String name){TextView h=text(name,12,ACCENT);h.setPadding(dp(16),dp(22),dp(16),dp(8));parent.addView(h);}
    LinearLayout row(LinearLayout parent,String title,String value,Runnable click){LinearLayout box=column();box.setBackgroundColor(PANEL);box.setPadding(dp(16),dp(12),dp(16),dp(12));box.addView(text(title,16,TEXT));TextView detail=text(value,13,MUTED);detail.setPadding(0,dp(3),0,0);box.addView(detail);if(click!=null){box.setFocusable(true);box.setClickable(true);box.setOnClickListener(v->{if(!working)click.run();});box.setContentDescription(title+", "+value);}parent.addView(box,new LinearLayout.LayoutParams(-1,-2));View line=new View(this);line.setBackgroundColor(DIVIDER);parent.addView(line,new LinearLayout.LayoutParams(-1,dp(1)));return box;}
    void note(LinearLayout parent,String value){TextView t=text(value,12,MUTED);t.setPadding(dp(16),dp(10),dp(16),dp(10));parent.addView(t);}
    void switchRow(LinearLayout parent,String title,String helper,String key,boolean initial){LinearLayout r=new LinearLayout(this);r.setGravity(Gravity.CENTER_VERTICAL);r.setBackgroundColor(PANEL);r.setPadding(dp(16),dp(12),dp(12),dp(12));LinearLayout words=column();words.setBackgroundColor(Color.TRANSPARENT);words.addView(text(title,16,TEXT));if(!helper.isEmpty())words.addView(text(helper,12,MUTED));r.addView(words,new LinearLayout.LayoutParams(0,-2,1));Switch s=new Switch(this);s.setChecked(prefs().getBoolean(key,initial));s.setContentDescription(title);s.setOnCheckedChangeListener((b,value)->prefs().edit().putBoolean(key,value).apply());r.addView(s);parent.addView(r);}
    void launchRom(String name){if(!access())return;if(SessionGuard.busy()){String active=SessionGuard.filename();if(name.equals(active))startActivity(new Intent(this,EmulatorActivity.class).putExtra("rom",name).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));else error("Exit the active session before starting another ROM.");return;}
        prepareAndLaunch(name,RomRepository.SaveChoice.NEW_INSTALLATION);
    }
    private void prepareAndLaunch(String name,RomRepository.SaveChoice choice){task(()->{RomRepository r=repository();r.prepareSave(name,choice);return r.verifyForLaunch(name);},p->startActivity(new Intent(this,EmulatorActivity.class).putExtra("rom",p.filename)));}
    interface FolderChoice{void chosen(File file);}
    void folder(File start,FolderChoice choice){if(!access())return;task(()->{
        File f=start;while(f!=null&&!f.isDirectory())f=f.getParentFile();if(f==null)throw new IOException("Folder cannot be opened: "+start);ProfileFiles.plain(f);File[] files=f.listFiles(File::isDirectory);if(files==null)throw new IOException("Folder cannot be opened");Arrays.sort(files,Comparator.comparing(File::getName,String.CASE_INSENSITIVE_ORDER));return new FolderListing(f,files);
    },listing->{List<String> labels=new ArrayList<>();labels.add("↑ Parent folder");for(File f:listing.children)labels.add(f.getName());new AlertDialog.Builder(this).setTitle(listing.current.getPath()).setItems(labels.toArray(new String[0]),(d,index)->{File next=index==0?listing.current.getParentFile():listing.children[index-1];if(next!=null)folder(next,choice);}).setPositiveButton("Use this folder",(d,w)->task(()->repository().validateCard(listing.current),choice::chosen)).setNegativeButton("Cancel",null).setNeutralButton("Path…",(d,w)->{EditText input=new EditText(this);input.setSingleLine(true);input.setText(listing.current.getPath());new AlertDialog.Builder(this).setTitle("Folder path").setView(input).setPositiveButton("Open",(dialog,x)->folder(new File(input.getText().toString()),choice)).setNegativeButton("Cancel",null).show();}).show();});}
    static final class FolderListing{final File current;final File[] children;FolderListing(File f,File[] files){current=f;children=files;}}
    static final Map<String,Bitmap> icons=new HashMap<>();
    Bitmap icon(String id){synchronized(icons){Bitmap b=icons.get(id);if(b==null){String color=id.replace("pda_","");int res=getResources().getIdentifier("rom_icon_"+color,"drawable",getPackageName());if(res!=0){b=BitmapFactory.decodeResource(getResources(),res);icons.put(id,b);}}return b;}}
    @Override protected void onDestroy(){destroyed=true;io.shutdown();super.onDestroy();}
}
