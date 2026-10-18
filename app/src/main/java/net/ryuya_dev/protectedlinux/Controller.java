package net.ryuya_dev.protectedlinux;
import android.app.Activity;
import android.content.Context;
import android.net.*;
import android.os.*;
import android.util.Base64;
import android.view.View;
import android.webkit.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.zip.ZipFile;

final class Controller {
 private static final Map<Activity,Controller> active=new WeakHashMap<>();
 private final Semaphore outputSlots=new Semaphore(8);
 private final Context context;private final String apk;
 private final ExecutorService io=Executors.newSingleThreadExecutor(),input=new ThreadPoolExecutor(1,1,0L,TimeUnit.MILLISECONDS,new ArrayBlockingQueue<>(32));
 private volatile LocalSocket socket;private volatile boolean closed;
 private WebView web;private Object fragment;
 private Controller(Context c,String path){context=c;apk=path;}
 static Controller prepare(Activity a,Context c,String path){Controller ctl=active.get(a);if(ctl==null){ctl=new Controller(c,path);active.put(a,ctl);}return ctl;}
 static void bind(Activity a,Object f,Context c,String path){Controller ctl=prepare(a,c,path);try{ctl.attach(a,f);}catch(Throwable e){HookEntry.status(c,"error");android.util.Log.e("ProtectedLinux","VIEW_STOP "+e.getClass().getSimpleName());}}
 static void close(Activity a){Controller ctl=active.remove(a);if(ctl!=null)ctl.shutdown();}
 private static Field field(Object o,String name)throws Exception{Field f=o.getClass().getDeclaredField(name);f.setAccessible(true);return f;}
 private String asset(String path)throws Exception{try(ZipFile z=new ZipFile(apk);InputStream in=z.getInputStream(z.getEntry("assets/"+path))){return new String(in.readAllBytes(),StandardCharsets.UTF_8);}}
 private void attach(Activity a,Object f)throws Exception{
  if(web!=null){((View)field(f,"bootProgressView").get(f)).setVisibility(View.GONE);return;}
  fragment=f;web=(WebView)field(f,"terminalView").get(f);
  field(f,"ttydStatus").setInt(f,2);web.removeCallbacks((Runnable)field(f,"ttydTimeoutRunnable").get(f));
  ((View)field(f,"bootProgressView").get(f)).setVisibility(View.GONE);web.setVisibility(View.VISIBLE);
  View add=(View)field(a,"tabAddButton").get(a);if(add!=null)add.setEnabled(false);
  int settings=a.getResources().getIdentifier("settings_button","id",RuntimeSetup.HOST);View settingsButton=a.findViewById(settings);if(settingsButton!=null)settingsButton.setEnabled(false);
  web.getSettings().setJavaScriptEnabled(true);web.getSettings().setAllowFileAccess(false);web.getSettings().setAllowContentAccess(false);web.getSettings().setBlockNetworkLoads(true);
  web.addJavascriptInterface(new ConsoleApi(),"ProtectedConsole");
  web.setWebViewClient(new WebViewClient(){
   public boolean shouldOverrideUrlLoading(WebView w,WebResourceRequest r){return true;}
   public WebResourceResponse shouldInterceptRequest(WebView w,WebResourceRequest r){return new WebResourceResponse("text/plain","UTF-8",new ByteArrayInputStream(new byte[0]));}
   public void onPageFinished(WebView w,String u){try{field(fragment,"ttydStatus").setInt(fragment,2);w.removeCallbacks((Runnable)field(fragment,"ttydTimeoutRunnable").get(fragment));Method map=w.getClass().getMethod("mapCtrlKey");map.invoke(w);}catch(Exception ignored){}}
  });
  String html="<!doctype html><html><head><title>Protected Linux</title><meta name=viewport content='width=device-width,initial-scale=1,maximum-scale=1'><meta http-equiv=Content-Security-Policy content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; font-src data:; connect-src 'none'\"><style>"+asset("web/xterm.css")+"html,body{margin:0;height:100%;background:#101014;color:#ddd;overflow:auto}#terminal{height:100%;padding:6px;box-sizing:border-box}</style><script>"+asset("web/xterm.js")+"</script></head><body><div id=terminal></div><script>window.term=new Terminal({cols:80,rows:24,fontSize:Math.max(7,Math.min(14,(innerWidth-16)/48.2)),convertEol:false,scrollback:3000,theme:{background:'#101014'}});term.open(document.getElementById('terminal'));term.onData(s=>ProtectedConsole.write(s));addEventListener('resize',()=>{term.options.fontSize=Math.max(7,Math.min(14,(innerWidth-16)/48.2));});window.acceptBytes=s=>term.write(Uint8Array.from(atob(s),c=>c.charCodeAt(0)));ProtectedConsole.ready();</script></body></html>";
  web.loadDataWithBaseURL("https://protected-linux.invalid/",html,"text/html","UTF-8",null);
 }
 public final class ConsoleApi {
  @JavascriptInterface public void ready(){if(!closed)try{io.submit(Controller.this::connect);}catch(RejectedExecutionException ignored){}}
  @JavascriptInterface public void write(String data){if(data==null||data.length()>65536||closed)return;try{input.submit(()->{try{LocalSocket s=socket;if(s!=null)s.getOutputStream().write(data.getBytes(StandardCharsets.UTF_8));}catch(IOException e){message("\r\n[接続が終了しました]\r\n");}});}catch(RejectedExecutionException full){disconnect(socket);message("\r\n[Input queue exceeded; console disconnected]\r\n");}}
 }
 private void message(String text){emit(text.getBytes(StandardCharsets.UTF_8));}
 private void emit(byte[] data){if(closed)return;try{outputSlots.acquire();}catch(InterruptedException e){Thread.currentThread().interrupt();return;}String code="acceptBytes('"+Base64.encodeToString(data,Base64.NO_WRAP)+"')";WebView v=web;if(v!=null&&!closed)v.post(()->{if(!closed)v.evaluateJavascript(code,result->outputSlots.release());else outputSlots.release();});else outputSlots.release();}
 private void connect(){
  if(closed||socket!=null)return;
  message("保護VMを準備しています…\r\n検証版: Debianは読み取り専用、ネットワーク・GUI・複数タブは未対応です。\r\n");
  try{
   if(!HookEntry.configReady())throw new IOException("Vectorとの設定通信が未準備です。Protected Linux Bridgeを開いて確認してください。");
   LocalSocket s=new LocalSocket();
   try{s.connect(new LocalSocketAddress("net.ryuya_dev.protectedlinux.console",LocalSocketAddress.Namespace.ABSTRACT));}
   catch(IOException unavailable){s.close();throw new IOException("Protected Linux Bridgeを開いてrootと起動支援を準備してください。",unavailable);}
   if(s.getPeerCredentials().getUid()!=0){s.close();throw new IOException("起動支援の接続先を確認できません。");}
   s.getOutputStream().write('R');
   socket=s;HookEntry.status(context,"connected");android.util.Log.i("ProtectedLinux","CONSOLE_CONNECTED");
   byte[] buffer=new byte[16384];int n;
   while(!closed&&(n=s.getInputStream().read(buffer))>=0){if(n>0)emit(Arrays.copyOf(buffer,n));}
   message("\r\n[Linuxコンソールは終了しました。開き直すと再起動します]\r\n");HookEntry.status(context,"closed");
  }catch(Exception error){message("\r\n起動できません: "+error.getMessage()+"\r\nAndroid本体の再起動は不要です。\r\n");HookEntry.status(context,"error");android.util.Log.e("ProtectedLinux","CONSOLE_STOP "+error.getClass().getSimpleName());}
  finally{LocalSocket s=socket;socket=null;if(s!=null)try{s.close();}catch(IOException ignored){}}
 }
 private static void disconnect(LocalSocket s){if(s==null)return;try{s.shutdownOutput();}catch(IOException ignored){}try{s.shutdownInput();}catch(IOException ignored){}try{s.close();}catch(IOException ignored){}}
 private void shutdown(){android.util.Log.i("ProtectedLinux","CONSOLE_CLOSING");closed=true;disconnect(socket);io.shutdownNow();input.shutdownNow();web=null;}
}
