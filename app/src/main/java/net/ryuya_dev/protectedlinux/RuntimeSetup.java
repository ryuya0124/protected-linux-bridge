package net.ryuya_dev.protectedlinux;

import android.content.Context;
import java.io.*;
import java.nio.file.*;
import java.util.concurrent.*;

final class RuntimeSetup {
  static final String HOST = "com.android.virtualization.terminal";
  private static Process process;
  private static boolean ready;
  static synchronized String start(Context context) {
    if (!context.getSharedPreferences("config",0).getBoolean("enabled",true)) return "disabled";
    if (process != null && process.isAlive() && ready) return "ready";
    try {
      if (android.os.Build.VERSION.SDK_INT!=36||context.getPackageManager().getPackageInfo(HOST,0).getLongVersionCode()!=36)return "unsupported_version";
      int uid = context.getPackageManager().getApplicationInfo(HOST,0).uid;
      if (uid < 10000 || uid > 19999) return "primary_user_required";
      File runtime = new File(context.getFilesDir(),"runtime");
      if (!runtime.isDirectory() && !runtime.mkdirs()) return "runtime_directory_error";
      for (String name : new String[]{"debian-bridge","debian-initramfs.cpio","gpu-server","gpu-libepoxy.so","gpu-libvirglrenderer.so"}) {
        File out = new File(runtime,name+".pending");
        try (InputStream input=context.getAssets().open("runtime/"+name)) {
          Files.copy(input,out.toPath(),StandardCopyOption.REPLACE_EXISTING);
        }
        out.setReadable(false,false);out.setReadable(true,true);
        if (name.equals("debian-bridge")) out.setExecutable(true,true);
        Files.move(out.toPath(),new File(runtime,name).toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
      }
      String base="/data/user/0/net.ryuya_dev.protectedlinux/files/vm";
      // All paths are internal fixed package paths; the UID comes from PackageManager.
      String command=runtime.getAbsolutePath()+"/debian-bridge --check || { "+runtime.getAbsolutePath()+"/debian-bridge --stop && umask 077 && mkdir -p "+base+" && chmod 700 "+base+
        " && cp "+runtime.getAbsolutePath()+"/debian-initramfs.cpio "+base+"/debian-initramfs.cpio"+
        " && exec "+runtime.getAbsolutePath()+"/debian-bridge "+uid+" "+android.os.Process.myUid()+"; }";
      process = new ProcessBuilder("su","-M","-c",command).redirectErrorStream(true).start();
      Process current=process;
      ExecutorService reader=Executors.newSingleThreadExecutor();
      try {
        String line=reader.submit(() -> new BufferedReader(new InputStreamReader(current.getInputStream())).readLine()).get(25,TimeUnit.SECONDS);
        ready=line!=null&&(line.equals("BRIDGE_WAITING uid="+uid)||line.equals("BRIDGE_WAITING existing"));if(!ready)android.util.Log.w("ProtectedLinux","SETUP_REJECTED "+line);
      } finally { reader.shutdownNow(); }
      if (!ready) { current.destroyForcibly(); return "root_or_runtime_unavailable"; }
      Thread diagnostics=new Thread(()->{try(BufferedReader lines=new BufferedReader(new InputStreamReader(current.getInputStream()))){String line;while((line=lines.readLine())!=null)if(line.startsWith("PROXY_"))android.util.Log.w("ProtectedLinux",line);}catch(IOException ignored){}},"plb-runtime-diagnostics");diagnostics.setDaemon(true);diagnostics.start();
      return "ready";
    } catch (Exception error) {
      if(process!=null)process.destroyForcibly();ready=false;
      android.util.Log.w("ProtectedLinux","SETUP_STOP "+error.getClass().getSimpleName()+": "+error.getMessage());
      return "root_or_runtime_unavailable";
    }
  }
  static synchronized boolean saveGraphics(Context context,int mode,int resolution,int fps){
    if((mode!=0&&mode!=1&&mode!=3)||resolution<0||resolution>8||!java.util.Set.of(15,24,30,60,90,120).contains(fps))return false;
    try{
      if(!"ready".equals(start(context)))return false;
      String binary=new File(context.getFilesDir(),"runtime/debian-bridge").getAbsolutePath();
      Process p=new ProcessBuilder("su","-M","-c",binary+" --settings "+mode+" "+resolution+" "+fps).redirectErrorStream(true).start();
      boolean ok=p.waitFor(8,TimeUnit.SECONDS)&&p.exitValue()==0;if(p.isAlive())p.destroyForcibly();return ok;
    }catch(Exception error){return false;}
  }
  record GraphicsState(int mode,int resolution,int fps){}
  static synchronized GraphicsState graphicsState(Context context){
    try{File binary=new File(context.getFilesDir(),"runtime/debian-bridge");if(!binary.isFile())return new GraphicsState(-1,2,30);
      Process p=new ProcessBuilder("su","-M","-c",binary.getAbsolutePath()+" --graphics-state").redirectErrorStream(true).start();
      if(!p.waitFor(8,TimeUnit.SECONDS)){p.destroyForcibly();return new GraphicsState(-2,2,30);}String output=new String(p.getInputStream().readAllBytes(),java.nio.charset.StandardCharsets.UTF_8).trim();
      if(p.exitValue()!=0||!output.matches("GRAPHICS (-1|0|1|3) [0-8] (15|24|30|60|90|120)"))return new GraphicsState(-2,2,30);String[] parts=output.split(" ");return new GraphicsState(Integer.parseInt(parts[1]),Integer.parseInt(parts[2]),Integer.parseInt(parts[3]));
    }catch(Exception error){return new GraphicsState(-2,2,30);}
  }
  static boolean reopenTerminal(){
    try{Process p=new ProcessBuilder("su","-M","-c","am force-stop "+HOST+" && am start -n "+HOST+"/.MainActivity").redirectErrorStream(true).start();if(!p.waitFor(10,TimeUnit.SECONDS)){p.destroyForcibly();return false;}return p.exitValue()==0;}catch(Exception error){return false;}
  }
  static synchronized boolean stop(Context context){
    try {
      File binary=new File(context.getFilesDir(),"runtime/debian-bridge");if(!binary.isFile())return false;
      Process shutdown=new ProcessBuilder("su","-M","-c",binary.getAbsolutePath()+" --stop").redirectErrorStream(true).start();
      boolean ok=shutdown.waitFor(20,TimeUnit.SECONDS)&&shutdown.exitValue()==0;
      if(shutdown.isAlive())shutdown.destroyForcibly();if(ok){ready=false;process=null;}return ok;
    }catch(Exception failure){return false;}
  }
}
