package net.ryuya_dev.protectedlinux;

import android.content.Context;
import java.io.*;
import java.nio.file.*;
import java.util.concurrent.*;

final class RuntimeSetup {
  static final String HOST = "com.android.virtualization.terminal";
  private static Process process;
  private static boolean ready;
  static File binary(Context c){return new File(c.getFilesDir(),"runtime/"+(c.getSharedPreferences("config",0).getBoolean("mainline_kernel",false)?"debian-mainline-bridge":"debian-bridge"));}
  static synchronized String start(Context context) {
    if (!context.getSharedPreferences("config",0).getBoolean("enabled",true)) return "disabled";
    if (process != null && process.isAlive() && ready) return "ready";
    try {
      if(!java.util.Arrays.asList(android.os.Build.SUPPORTED_64_BIT_ABIS).contains("arm64-v8a"))return "unsupported_architecture";
      if (android.os.Build.VERSION.SDK_INT!=36||context.getPackageManager().getPackageInfo(HOST,0).getLongVersionCode()!=36)return "unsupported_version";
      int uid = context.getPackageManager().getApplicationInfo(HOST,0).uid;
      if (uid < 10000 || uid > 19999) return "primary_user_required";
      File runtime = new File(context.getFilesDir(),"runtime");
      if (!runtime.isDirectory() && !runtime.mkdirs()) return "runtime_directory_error";
      for (String name : new String[]{"debian-bridge","debian-mainline-bridge","debian-initramfs.cpio","gpu-server","gpu-libepoxy.so","gpu-libvirglrenderer.so","clipboard-worker.jar"}) {
        File out = new File(runtime,name+".pending");
        try (InputStream input=context.getAssets().open("runtime/"+name)) {
          Files.copy(input,out.toPath(),StandardCopyOption.REPLACE_EXISTING);
        }
        out.setReadable(false,false);out.setReadable(true,true);
        if (name.equals("debian-bridge")||name.equals("debian-mainline-bridge")) out.setExecutable(true,true);
        Files.move(out.toPath(),new File(runtime,name).toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
      }
      String base="/data/user/0/net.ryuya_dev.protectedlinux/files/"+(context.getSharedPreferences("config",0).getBoolean("mainline_kernel",false)?"vm-mainline":"vm");
      String executable=binary(context).getAbsolutePath();
      // All paths are internal fixed package paths; the UID comes from PackageManager.
      String command=executable+" --check || { "+executable+" --stop && umask 077 && mkdir -p "+base+" && chmod 700 "+base+
        " && cp "+runtime.getAbsolutePath()+"/debian-initramfs.cpio "+base+"/debian-initramfs.cpio"+
        " && exec "+executable+" "+uid+" "+android.os.Process.myUid()+"; }";
      process = new ProcessBuilder("su","-M","-c",command).redirectErrorStream(true).start();
      Process current=process;
      ExecutorService reader=Executors.newSingleThreadExecutor();
      try {
        String line=reader.submit(() -> new BufferedReader(new InputStreamReader(current.getInputStream())).readLine()).get(50,TimeUnit.SECONDS);
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
      String binary=binary(context).getAbsolutePath();
      Process p=new ProcessBuilder("su","-M","-c",binary+" --settings "+mode+" "+resolution+" "+fps).redirectErrorStream(true).start();
      boolean ok=p.waitFor(8,TimeUnit.SECONDS)&&p.exitValue()==0;if(p.isAlive())p.destroyForcibly();return ok;
    }catch(Exception error){return false;}
  }
  record GraphicsState(int mode,int resolution,int fps){}
  static synchronized GraphicsState graphicsState(Context context){
    try{File binary=binary(context);if(!binary.isFile())return new GraphicsState(-1,2,30);
      Process p=new ProcessBuilder("su","-M","-c",binary.getAbsolutePath()+" --graphics-state").redirectErrorStream(true).start();
      if(!p.waitFor(8,TimeUnit.SECONDS)){p.destroyForcibly();return new GraphicsState(-2,2,30);}String output=new String(p.getInputStream().readAllBytes(),java.nio.charset.StandardCharsets.UTF_8).trim();
      if(p.exitValue()!=0||!output.matches("GRAPHICS (-1|0|1|3) [0-8] (15|24|30|60|90|120)"))return new GraphicsState(-2,2,30);String[] parts=output.split(" ");return new GraphicsState(Integer.parseInt(parts[1]),Integer.parseInt(parts[2]),Integer.parseInt(parts[3]));
    }catch(Exception error){return new GraphicsState(-2,2,30);}
  }
  record Resources(int memory,int cpus,int minutes,int disk,boolean keepAlive,boolean systemd){}
  static Resources resources(Context context){
    try{File binary=binary(context);Process p=new ProcessBuilder("su","-M","-c",binary.getAbsolutePath()+" --resources-state").redirectErrorStream(true).start();if(!p.waitFor(8,TimeUnit.SECONDS)){p.destroyForcibly();return null;}String[] a=new String(p.getInputStream().readAllBytes(),java.nio.charset.StandardCharsets.UTF_8).trim().split(" ");if(p.exitValue()!=0||a.length!=7||!a[0].equals("RESOURCES"))return null;Resources r=new Resources(Integer.parseInt(a[1]),Integer.parseInt(a[2]),Integer.parseInt(a[3]),Integer.parseInt(a[4]),a[5].equals("1"),a[6].equals("1"));return validResources(r)?r:null;}catch(Exception error){return null;}
  }
  private static boolean validResources(Resources r){return java.util.Set.of(512,1024,2048,4096,8192).contains(r.memory())&&java.util.Set.of(1,2,4,8).contains(r.cpus())&&java.util.Set.of(0,15,30,60,120,360).contains(r.minutes())&&java.util.Set.of(0,4,8,16,32).contains(r.disk());}
  static boolean saveResources(Context context,Resources r){
    if(!validResources(r))return false;try{if(!"ready".equals(start(context)))return false;String binary=binary(context).getAbsolutePath();Process p=new ProcessBuilder("su","-M","-c",binary+" --resources "+r.memory()+" "+r.cpus()+" "+r.minutes()+" "+r.disk()+" "+(r.keepAlive()?1:0)+" "+(r.systemd()?1:0)).redirectErrorStream(true).start();boolean ok=p.waitFor(8,TimeUnit.SECONDS)&&p.exitValue()==0;if(p.isAlive())p.destroyForcibly();return ok;}catch(Exception error){return false;}
  }
  static boolean clipboard(Context context,int mode){
    if(mode<0||mode>3)return false;
    try{String binary=binary(context).getAbsolutePath();Process p=new ProcessBuilder("su","-M","-c",binary+" --clipboard "+mode).redirectErrorStream(true).start();boolean ok=p.waitFor(8,TimeUnit.SECONDS)&&p.exitValue()==0;if(p.isAlive())p.destroyForcibly();if(!ok||mode==0)return ok;
      for(int i=0;i<20;i++){try(android.net.LocalSocket socket=new android.net.LocalSocket()){socket.connect(new android.net.LocalSocketAddress("net.ryuya_dev.protectedlinux.clipboard",android.net.LocalSocketAddress.Namespace.ABSTRACT));if(socket.getPeerCredentials().getUid()!=2000)return false;socket.setSoTimeout(1000);socket.getOutputStream().write('?');return socket.getInputStream().read()==mode;}catch(IOException unavailable){Thread.sleep(100);}}
      return false;
    }catch(Exception error){android.util.Log.w("ProtectedLinux","CLIPBOARD_START_FAILED "+error.getClass().getSimpleName());return false;}
  }
  static boolean reopenTerminal(){
    try{Process p=new ProcessBuilder("su","-M","-c","am force-stop "+HOST+" && am start -n "+HOST+"/.MainActivity").redirectErrorStream(true).start();if(!p.waitFor(10,TimeUnit.SECONDS)){p.destroyForcibly();return false;}return p.exitValue()==0;}catch(Exception error){return false;}
  }
  static synchronized boolean stop(Context context){
    try {
      File binary=binary(context);if(!binary.isFile())return false;
      Process shutdown=new ProcessBuilder("su","-M","-c",binary.getAbsolutePath()+" --stop").redirectErrorStream(true).start();
      boolean ok=shutdown.waitFor(45,TimeUnit.SECONDS)&&shutdown.exitValue()==0;
      if(shutdown.isAlive())shutdown.destroyForcibly();if(ok){ready=false;process=null;}return ok;
    }catch(Exception failure){return false;}
  }
}
