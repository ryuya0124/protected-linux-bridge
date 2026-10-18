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
      for (String name : new String[]{"debian-bridge","debian-initramfs.cpio"}) {
        File out = new File(runtime,name);
        try (InputStream input=context.getAssets().open("runtime/"+name)) {
          Files.copy(input,out.toPath(),StandardCopyOption.REPLACE_EXISTING);
        }
        out.setReadable(false,false);out.setReadable(true,true);
        if (name.equals("debian-bridge")) out.setExecutable(true,true);
      }
      String base="/data/user/0/"+HOST+"/files/protected-linux";
      // All paths are internal fixed package paths; the UID comes from PackageManager.
      String command=runtime.getAbsolutePath()+"/debian-bridge --check || { umask 077; mkdir -p "+base+" && chmod 700 "+base+
        " && cp "+runtime.getAbsolutePath()+"/debian-initramfs.cpio "+base+"/debian-initramfs.cpio"+
        " && exec "+runtime.getAbsolutePath()+"/debian-bridge "+uid+" "+android.os.Process.myUid()+"; }";
      process = new ProcessBuilder("su","-M","-c",command).redirectErrorStream(true).start();
      Process current=process;
      ExecutorService reader=Executors.newSingleThreadExecutor();
      try {
        String line=reader.submit(() -> new BufferedReader(new InputStreamReader(current.getInputStream())).readLine()).get(12,TimeUnit.SECONDS);
        ready=line!=null&&(line.equals("BRIDGE_WAITING uid="+uid)||line.equals("BRIDGE_WAITING existing"));if(!ready)android.util.Log.w("ProtectedLinux","SETUP_REJECTED "+line);
      } finally { reader.shutdownNow(); }
      if (!ready) { current.destroyForcibly(); return "root_or_runtime_unavailable"; }
      return "ready";
    } catch (Exception error) {
      if(process!=null)process.destroyForcibly();ready=false;
      android.util.Log.w("ProtectedLinux","SETUP_STOP "+error.getClass().getSimpleName()+": "+error.getMessage());
      return "root_or_runtime_unavailable";
    }
  }
}
