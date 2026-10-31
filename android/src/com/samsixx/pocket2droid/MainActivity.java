package com.samsixx.pocket2droid;
import android.content.*;
import android.database.Cursor;
import android.os.*;
import android.provider.OpenableColumns;
import android.text.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;

public final class MainActivity extends LibraryUi {
    private static final int PICK_ROM=10,PICK_CARD_FILE=11;
    private final List<RomProfile> profiles=new ArrayList<>(),visible=new ArrayList<>();
    private EditText search;
    private BaseAdapter adapter;
    private TextView empty;
    private Button resume;
    private ImageButton addButton;
    private PopupWindow addPopup;
    private boolean picking;
    private boolean startupAccessRequested;
    @Override public void onCreate(Bundle saved){super.onCreate(saved);picking=saved!=null&&saved.getBoolean("picking",false);startupAccessRequested=saved!=null&&saved.getBoolean("startup_access_requested",false);LinearLayout root=column();
        LinearLayout toolbar=new LinearLayout(this);toolbar.setGravity(Gravity.CENTER_VERTICAL);toolbar.setPadding(dp(12),0,dp(4),0);toolbar.setBackgroundColor(TOOLBAR);
        TextView title=text("Pocket2Droid",21,TOOLBAR_TEXT);title.setSingleLine(true);title.setEllipsize(TextUtils.TruncateAt.END);toolbar.addView(title,new LinearLayout.LayoutParams(0,dp(54),1));title.setGravity(Gravity.CENTER_VERTICAL);
        addButton=iconButton(ToolbarIcon.Symbol.ADD,"Add ROM or file",this::showAddMenu);toolbar.addView(addButton,new LinearLayout.LayoutParams(dp(48),dp(48)));
        toolbar.addView(iconButton(ToolbarIcon.Symbol.SEARCH,"Search ROMs",()->{search.setVisibility(search.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE);if(search.getVisibility()==View.VISIBLE)search.requestFocus();else search.setText("");}),new LinearLayout.LayoutParams(dp(48),dp(48)));
        toolbar.addView(iconButton(ToolbarIcon.Symbol.SETTINGS,"App settings",()->startActivity(new Intent(this,AppSettingsActivity.class))),new LinearLayout.LayoutParams(dp(48),dp(48)));root.addView(toolbar);
        search=new EditText(this);search.setTextColor(TEXT);search.setHintTextColor(MUTED);search.setHint("Search ROMs");search.setSingleLine(true);search.setVisibility(View.GONE);root.addView(search);search.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int st,int c,int a){}public void onTextChanged(CharSequence s,int st,int before,int count){filter();}public void afterTextChanged(Editable e){}});
        resume=action("Resume active ROM",()->{String name=SessionGuard.filename();if(name!=null)launchRom(name);});resume.setVisibility(View.GONE);root.addView(resume);
        ListView list=new ListView(this);list.setDivider(new android.graphics.drawable.ColorDrawable(DIVIDER));list.setDividerHeight(dp(1));list.setBackgroundColor(PANEL);list.setDrawSelectorOnTop(true);
        android.graphics.drawable.StateListDrawable selector=new android.graphics.drawable.StateListDrawable();selector.addState(new int[]{android.R.attr.state_pressed},new android.graphics.drawable.ColorDrawable(0x6054a7ff));selector.addState(new int[]{android.R.attr.state_focused,android.R.attr.state_window_focused},new android.graphics.drawable.ColorDrawable(0x6054a7ff));selector.addState(new int[]{},new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));list.setSelector(selector);
        adapter=new BaseAdapter(){public int getCount(){return visible.size();}public Object getItem(int p){return visible.get(p);}public long getItemId(int p){return p;}public View getView(int position,View recycled,ViewGroup parent){RomProfile p=visible.get(position);LinearLayout row=new LinearLayout(MainActivity.this);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(dp(14),dp(12),dp(12),dp(12));row.setBackgroundColor(PANEL);ImageView image=new ImageView(MainActivity.this);image.setImageBitmap(icon(p.iconId));row.addView(image,new LinearLayout.LayoutParams(dp(44),dp(44)));LinearLayout words=column();words.setBackgroundColor(android.graphics.Color.TRANSPARENT);words.setPadding(dp(14),0,0,0);words.addView(text(p.displayName,17,TEXT));words.addView(text(p.filename,12,MUTED));if(p.glesBuiltIn)glesBadge(words);if(p.error!=null)words.addView(text(p.error,11,0xffffaa88));row.addView(words,new LinearLayout.LayoutParams(0,-2,1));row.addView(text("›",22,MUTED));return row;}};
        list.setAdapter(adapter);list.setOnItemClickListener((p,v,pos,id)->{RomProfile rom=visible.get(pos);clearPress(list,v);if(rom.error!=null)error(rom.error);else launchRom(rom.filename);});list.setOnItemLongClickListener((p,v,pos,id)->{if(working)return true;final String name=visible.get(pos).filename;clearPress(list,v);PopupMenu menu=new PopupMenu(this,v,Gravity.END);menu.getMenu().add("Settings");menu.setOnMenuItemClickListener(item->{startActivity(new Intent(this,RomSettingsActivity.class).putExtra("rom",name));return true;});menu.show();clearPress(list,v);return true;});
        root.addView(list,new LinearLayout.LayoutParams(-1,0,1));empty=text("Add a .bin ROM using +, or place .bin files in pocket2droid/Roms.",14,MUTED);empty.setPadding(dp(16),dp(12),dp(16),dp(12));root.addView(empty);status=text("",12,MUTED);status.setVisibility(View.GONE);status.setPadding(dp(16),dp(4),dp(16),dp(4));root.addView(status);TextView hint=text("Tap to start · Hold for options",12,MUTED);hint.setPadding(dp(16),dp(3),dp(16),dp(3));root.addView(hint);install(root);
    }
    private void clearPress(ListView list,View row){list.setPressed(false);row.setPressed(false);list.jumpDrawablesToCurrentState();row.jumpDrawablesToCurrentState();}
    private void filter(){String query=search.getText().toString().toLowerCase(Locale.ROOT);visible.clear();for(RomProfile p:profiles)if(p.displayName.toLowerCase(Locale.ROOT).contains(query)||p.filename.toLowerCase(Locale.ROOT).contains(query))visible.add(p);adapter.notifyDataSetChanged();empty.setVisibility(visible.isEmpty()?View.VISIBLE:View.GONE);}
    private void refresh(){if(!StorageAccess.granted(this)){statusText("Allow storage access in App settings to load your ROM library.");return;}if(SessionGuard.busy()){resume.setVisibility(SessionGuard.filename()!=null?View.VISIBLE:View.GONE);statusText("A session or storage operation is active.");return;}resume.setVisibility(View.GONE);task(()->repository().scan(),result->{profiles.clear();profiles.addAll(result);filter();});}
    @Override protected void onResume(){super.onResume();if(adapter!=null&&!picking){if(!StorageAccess.granted(this)&&!startupAccessRequested){startupAccessRequested=true;access();}refresh();}}
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);startupAccessRequested=false;}
    @Override protected void onSaveInstanceState(Bundle out){out.putBoolean("picking",picking);out.putBoolean("startup_access_requested",startupAccessRequested);super.onSaveInstanceState(out);}
    private void showAddMenu(){if(working)return;if(addPopup!=null){addPopup.dismiss();return;}
        LinearLayout content=column();android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();background.setColor(PANEL);background.setCornerRadius(dp(8));content.setBackground(background);content.setClipToOutline(true);
        int preferred=Math.max(dp(248),(int)Math.ceil(text("",16,TEXT).getPaint().measureText("File to StorageCard"))+dp(72));int width=Math.min(preferred,Math.max(1,((View)addButton.getParent()).getWidth()-dp(24)));
        PopupWindow popup=new PopupWindow(content,width,LinearLayout.LayoutParams.WRAP_CONTENT,true);popup.setBackgroundDrawable(background);popup.setElevation(dp(8));popup.setOutsideTouchable(true);popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        addChoice(content,popup,"PPC Rom",ToolbarIcon.Symbol.PPC_ROM,PICK_ROM);View divider=new View(this);divider.setBackgroundColor(DIVIDER);content.addView(divider,new LinearLayout.LayoutParams(-1,dp(1)));addChoice(content,popup,"File to StorageCard",ToolbarIcon.Symbol.FILE,PICK_CARD_FILE);
        addPopup=popup;popup.setOnDismissListener(()->{if(addPopup==popup)addPopup=null;});popup.showAsDropDown(addButton,dp(96),dp(4),Gravity.END);
    }
    private void addChoice(LinearLayout parent,PopupWindow popup,String label,ToolbarIcon.Symbol symbol,int request){LinearLayout item=new LinearLayout(this);item.setGravity(Gravity.CENTER_VERTICAL);item.setPadding(dp(16),dp(12),dp(16),dp(12));item.setMinimumHeight(dp(52));item.setFocusable(true);item.setContentDescription(label);item.setBackground(new android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(0x3354a7ff),new android.graphics.drawable.ColorDrawable(PANEL),null));
        ImageView icon=new ImageView(this);icon.setImageDrawable(new ToolbarIcon(symbol,MUTED,dp(24)));icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);LinearLayout.LayoutParams image=new LinearLayout.LayoutParams(dp(24),dp(24));image.setMarginEnd(dp(16));item.addView(icon,image);TextView title=text(label,16,TEXT);title.setMaxLines(2);item.addView(title,new LinearLayout.LayoutParams(0,-2,1));item.setOnClickListener(view->{popup.dismiss();choose(request);});parent.addView(item,new LinearLayout.LayoutParams(-1,-2));
    }
    private void choose(int request){if(!access())return;if(SessionGuard.busy()){error("Exit the active session before adding a ROM or copying a file.");return;}Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");picking=true;try{startActivityForResult(i,request);}catch(RuntimeException failure){picking=false;error("A file picker is unavailable on this device.");}}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==StorageAccess.REQUEST){refresh();return;}if(request!=PICK_ROM&&request!=PICK_CARD_FILE)return;picking=false;if(result!=RESULT_OK||data==null||data.getData()==null){refresh();return;}final android.net.Uri uri=data.getData();task(()->{String name=null;try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst())name=c.getString(0);}if(request==PICK_ROM)RomRepository.binName(name);else RomRepository.cardFileName(name);try(InputStream in=getContentResolver().openInputStream(uri)){if(in==null)throw new IOException("Selected file cannot be opened");if(request==PICK_CARD_FILE)return "Copied "+repository().importCardFile(in,name).getName()+" to storagecard";return "Added "+repository().importRom(in,name).filename;}},message->{statusText(message);if(request==PICK_ROM)refresh();});}
    @Override protected void onDestroy(){if(addPopup!=null)addPopup.dismiss();super.onDestroy();}
    @Override public void onRequestPermissionsResult(int request,String[] p,int[] r){super.onRequestPermissionsResult(request,p,r);refresh();}
}
