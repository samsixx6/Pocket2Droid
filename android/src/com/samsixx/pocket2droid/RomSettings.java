package com.samsixx.pocket2droid;
import java.io.*;
import org.json.*;

final class RomSettings {
    enum ScaleMode { FITTED, PIXEL_PERFECT }
    enum HostOrientation { PORTRAIT, LANDSCAPE }
    enum ScreenPosition {
        TOP("Top",0f),CENTER("Center",0.5f),BOTTOM("Bottom",1f);
        final String label;final float anchor;
        ScreenPosition(String label,float anchor){this.label=label;this.anchor=anchor;}
    }
    enum GuestResolution {
        // Retained only to read profiles written before concrete preset defaults.
        ROM_DEFAULT("ROM default",0,0),SMARTPHONE("Smartphone (176×220)",176,220),
        SQUARE("Square (240×240)",240,240),QVGA("QVGA (240×320)",240,320),
        QVGA_LANDSCAPE("QVGA landscape (320×240)",320,240),SQUARE_QVGA("Square QVGA (320×320)",320,320),
        WQVGA("WQVGA (240×400)",240,400),SQUARE_VGA("Square VGA (480×480)",480,480),
        VGA("VGA (480×640)",480,640),WVGA("WVGA (480×800)",480,800),CUSTOM("Custom…",0,0);
        final String label;final int width,height;
        GuestResolution(String label,int w,int h){this.label=label;width=w;height=h;}
        boolean supported(){return true;}
        static GuestResolution[] presets(){GuestResolution[] all=values();return java.util.Arrays.copyOfRange(all,1,all.length-1);}
    }
    static final class GuestSize {
        final int width,height;
        GuestSize(int width,int height){this.width=width;this.height=height;}
    }
    final String displayName;
    final GuestResolution resolution;
    final int customWidth,customHeight;
    final ScaleMode scale;
    final HostOrientation orientation;
    final ScreenPosition screenPosition;
    final File card;
    final boolean internetEnabled;
    final boolean glesEnabled;
    final boolean glesGpuEnabled;
    final boolean networkSetupEligible;
    final String networkMac;
    RomSettings(String name,GuestResolution resolution,ScaleMode scale,HostOrientation orientation,File card)throws IOException {
        this(name,resolution,scale,orientation,ScreenPosition.CENTER,card);
    }
    RomSettings(String name,GuestResolution resolution,ScaleMode scale,HostOrientation orientation,ScreenPosition screenPosition,File card)throws IOException {
        this(name,resolution,scale,orientation,screenPosition,card,0,0,false,null,false,false,true);
    }
    private RomSettings(String name,GuestResolution resolution,ScaleMode scale,HostOrientation orientation,ScreenPosition screenPosition,File card,int customWidth,int customHeight,boolean internetEnabled,String networkMac,boolean networkSetupEligible,boolean glesEnabled,boolean glesGpuEnabled)throws IOException {
        if(name==null||name.trim().isEmpty()||name.length()>180||name.indexOf('\0')>=0)throw new IOException("Enter a nonempty display name (up to 180 characters)");
        if(resolution==null||scale==null||orientation==null||screenPosition==null||card==null||!card.isAbsolute())throw new IOException("Invalid ROM settings");
        if(!(resolution!=GuestResolution.CUSTOM&&customWidth==0&&customHeight==0)&&(customWidth<1||customHeight<1))throw new IOException("Width and height must be positive whole numbers");
        displayName=name.trim();this.resolution=resolution;this.scale=scale;this.orientation=orientation;this.screenPosition=screenPosition;this.card=card;
        this.customWidth=customWidth;this.customHeight=customHeight;
        if((networkMac!=null&&!NetworkConfig.validMac(networkMac))||(internetEnabled&&networkMac==null))throw new IOException("Invalid network identity in profile; existing data preserved");
        // Legacy flags remain readable, but graphics are automatic for every ROM.
        this.glesEnabled=true;this.glesGpuEnabled=true;this.networkSetupEligible=networkSetupEligible;this.internetEnabled=internetEnabled;this.networkMac=networkMac==null?null:networkMac.toLowerCase(java.util.Locale.ROOT);
    }
    static RomSettings defaults(String name,File card)throws IOException{return new RomSettings(name,GuestResolution.ROM_DEFAULT,ScaleMode.FITTED,HostOrientation.PORTRAIT,ScreenPosition.CENTER,card,0,0,true,NetworkConfig.newMac(new java.security.SecureRandom()),true,false,true);}
    RomSettings withDisplayName(String name)throws IOException{return new RomSettings(name,resolution,scale,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withScale(ScaleMode value)throws IOException{return new RomSettings(displayName,resolution,value,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withResolution(GuestResolution value)throws IOException{return new RomSettings(displayName,value,scale,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withCustomResolution(int width,int height)throws IOException{return new RomSettings(displayName,GuestResolution.CUSTOM,scale,orientation,screenPosition,card,width,height,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withInternetAccess(boolean enabled)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,screenPosition,card,customWidth,customHeight,enabled,enabled&&networkMac==null?NetworkConfig.newMac(new java.security.SecureRandom()):networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withNetworkSetupEligibility(boolean value)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,value,glesEnabled,glesGpuEnabled);}
    RomSettings withGles(boolean value)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,value,glesGpuEnabled);}
    RomSettings withGlesGpu(boolean value)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,value);}
    String resolutionLabel(){return resolution==GuestResolution.CUSTOM?"Custom ("+customWidth+"×"+customHeight+")":resolution.label;}
    GuestSize startupSize(String filename){GuestResolution preset=startupResolution(filename);return preset==GuestResolution.CUSTOM?new GuestSize(customWidth,customHeight):new GuestSize(preset.width,preset.height);}
    GuestResolution startupResolution(String filename){
        if(resolution!=GuestResolution.ROM_DEFAULT)return resolution;
        // SDK metadata supplies sizes outside the raw BIN. Recognize those names
        // with optional resolution annotations and import collision suffixes.
        // Unfamiliar filenames still default to QVGA.
        String name=filename.toUpperCase(java.util.Locale.ROOT)
            .replaceFirst(" \\([0-9]+\\)(?=\\.BIN$)","")
            .replaceFirst(" \\([0-9]+[X×][0-9]+\\)(?=\\.BIN$)","");
        switch(name){
            case "PPC_USA_GSM_SQUARE_VR.BIN":return GuestResolution.SQUARE;
            case "PPC_USA_GSM_QVGA_VR.BIN":case "PPC_USA_GSM_320X320_VR.BIN":return GuestResolution.SQUARE_QVGA;
            case "PPC_USA_GSM_VGA_SQUARE_VR.BIN":return GuestResolution.SQUARE_VGA;
            case "PPC_USA_GSM_VGA_VR.BIN":return GuestResolution.VGA;
            case "PPC_USA_GSM_WQVGA_VR.BIN":case "PPC-PHONE-240X400.BIN":return GuestResolution.WQVGA;
            case "PPC_USA_GSM_WVGA_VR.BIN":case "PPC-PHONE-480X800.BIN":return GuestResolution.WVGA;
            case "SP_USA_GSM_QVGA_LS_VR.BIN":return GuestResolution.QVGA_LANDSCAPE;
            case "SP_USA_GSM_VR.BIN":return GuestResolution.SMARTPHONE;
            default:return GuestResolution.QVGA;
        }
    }
    RomSettings withOrientation(HostOrientation value)throws IOException{return new RomSettings(displayName,resolution,scale,value,screenPosition,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withScreenPosition(ScreenPosition value)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,value,card,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    RomSettings withCard(File value)throws IOException{return new RomSettings(displayName,resolution,scale,orientation,screenPosition,value,customWidth,customHeight,internetEnabled,networkMac,networkSetupEligible,glesEnabled,glesGpuEnabled);}
    JSONObject json()throws JSONException{JSONObject o=new JSONObject().put("displayName",displayName).put("resolution",resolution.name()).put("scale",scale.name()).put("orientation",orientation.name()).put("screenPosition",screenPosition.name()).put("storageCard",card.getPath()).put("internetEnabled",internetEnabled).put("networkSetupEligible",networkSetupEligible).put("glesEnabled",glesEnabled).put("glesGpuEnabled",glesGpuEnabled);if(networkMac!=null)o.put("networkMac",networkMac);if(customWidth>0)o.put("customWidth",customWidth).put("customHeight",customHeight);return o;}
    private static int dimension(JSONObject o,String key)throws IOException,JSONException {
        Object value=o.get(key);
        if(!(value instanceof Integer)&&!(value instanceof Long))throw new IOException("Invalid custom resolution in profile; existing data preserved");
        long n=((Number)value).longValue();if(n<1||n>Integer.MAX_VALUE)throw new IOException("Invalid custom resolution in profile; existing data preserved");return (int)n;
    }
    static RomSettings parse(JSONObject o)throws IOException {
        try{GuestResolution mode=GuestResolution.valueOf(o.getString("resolution"));
            boolean dimensions=mode==GuestResolution.CUSTOM||o.has("customWidth")||o.has("customHeight");
            if(o.has("internetEnabled")&&!(o.get("internetEnabled") instanceof Boolean))throw new IOException("Invalid Internet access setting");
            if(o.has("networkSetupEligible")&&!(o.get("networkSetupEligible") instanceof Boolean))throw new IOException("Invalid automatic network setup setting");
            if(o.has("glesEnabled")&&!(o.get("glesEnabled") instanceof Boolean))throw new IOException("Invalid GLES setting");
            if(o.has("glesGpuEnabled")&&!(o.get("glesGpuEnabled") instanceof Boolean))throw new IOException("Invalid GLES renderer setting");
            if(o.has("networkMac")&&!(o.get("networkMac") instanceof String))throw new IOException("Invalid network identity");
            return new RomSettings(o.getString("displayName"),mode,ScaleMode.valueOf(o.getString("scale")),HostOrientation.valueOf(o.getString("orientation")),o.has("screenPosition")?ScreenPosition.valueOf(o.getString("screenPosition")):ScreenPosition.CENTER,new File(o.getString("storageCard")),dimensions?dimension(o,"customWidth"):0,dimensions?dimension(o,"customHeight"):0,o.has("internetEnabled")&&o.getBoolean("internetEnabled"),o.has("networkMac")?o.getString("networkMac"):null,o.has("networkSetupEligible")&&o.getBoolean("networkSetupEligible"),o.has("glesEnabled")&&o.getBoolean("glesEnabled"),!o.has("glesGpuEnabled")||o.getBoolean("glesGpuEnabled"));}
        catch(JSONException|IllegalArgumentException e){throw new IOException("Invalid profile settings; retained data preserved",e);}
    }
}
