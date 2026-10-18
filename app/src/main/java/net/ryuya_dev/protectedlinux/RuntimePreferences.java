package net.ryuya_dev.protectedlinux;
import android.content.SharedPreferences;
/** No Xposed types here: fullscreen also runs in the ordinary app process. */
final class RuntimePreferences {
 static volatile SharedPreferences remote;
 static boolean gpuDisplay(){try{return remote!=null?remote.getBoolean("gpu_display",false):BridgeApplication.local!=null&&BridgeApplication.local.getSharedPreferences("config",0).getBoolean("gpu_display",false);}catch(Exception error){return false;}}
 static int displayFps(){try{int fps=remote!=null?remote.getInt("display_fps",30):BridgeApplication.local!=null?BridgeApplication.local.getSharedPreferences("config",0).getInt("display_fps",30):30;return java.util.Set.of(15,24,30,60,90,120).contains(fps)?fps:30;}catch(Exception error){return 30;}}
 static boolean configReady(){try{return (remote!=null&&remote.contains("enabled"))||(BridgeApplication.local!=null&&BridgeApplication.local.getSharedPreferences("config",0).getBoolean("enabled",true));}catch(Exception e){return false;}}
}
