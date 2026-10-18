package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.util.Base64;
import android.view.View;
import android.webkit.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.Semaphore;
import java.util.zip.ZipFile;

final class Controller {
  private static final Map<Activity,Map<Object,Controller>> active=new WeakHashMap<>();
  private final Semaphore outputSlots=new Semaphore(8);
  private final String apk;
  private VmChannel channel;
  private volatile boolean closed;
  private WebView web;
  private Object fragment;
  private Controller(String path){apk=path;}
  static void prepare(Activity activity,Context ignored,String path){active.computeIfAbsent(activity,key->new HashMap<>());}
  static void bind(Activity activity,Object fragment,Context ignored,String path){
    Map<Object,Controller> pages=active.computeIfAbsent(activity,key->new HashMap<>());
    Controller controller=pages.computeIfAbsent(fragment,key->new Controller(path));
    try {controller.attach(activity,fragment);}catch(Exception error){android.util.Log.e("ProtectedLinux","VIEW_FAILED "+error.getClass().getSimpleName());controller.shutdown();}
  }
  static void close(Activity activity){Map<Object,Controller> pages=active.remove(activity);if(pages!=null)for(Controller controller:pages.values())controller.shutdown();DisplayController.close(activity);}
  static void closeFragment(Object fragment){for(Map<Object,Controller> pages:active.values()){Controller controller=pages.remove(fragment);if(controller!=null){controller.shutdown();return;}}}
  static Field field(Object owner,String name)throws Exception{Field field=owner.getClass().getDeclaredField(name);field.setAccessible(true);return field;}
  static byte[] asset(String apk,String path)throws IOException{if(path.contains("..")||path.startsWith("/"))throw new IOException("Invalid asset path");try(ZipFile zip=new ZipFile(apk)){var entry=zip.getEntry("assets/"+path);if(entry==null)throw new FileNotFoundException(path);try(InputStream stream=zip.getInputStream(entry)){return stream.readAllBytes();}}}
  private String textAsset(String name)throws IOException{return new String(asset(apk,name),StandardCharsets.UTF_8);}
  private void attach(Activity activity,Object page)throws Exception{
    if(web!=null)return;fragment=page;web=(WebView)field(page,"terminalView").get(page);
    field(page,"ttydStatus").setInt(page,2);web.removeCallbacks((Runnable)field(page,"ttydTimeoutRunnable").get(page));
    ((View)field(page,"bootProgressView").get(page)).setVisibility(View.GONE);web.setVisibility(View.VISIBLE);
    View add=(View)field(activity,"tabAddButton").get(activity);if(add!=null)add.setEnabled(true);
    View display=(View)field(activity,"displayMenu").get(activity);if(display!=null){display.setEnabled(true);display.setOnClickListener(v->DisplayController.open(activity,apk));}
    int settings=activity.getResources().getIdentifier("settings_button","id",RuntimeSetup.HOST);View settingsButton=activity.findViewById(settings);if(settingsButton!=null)settingsButton.setEnabled(false);
    web.getSettings().setJavaScriptEnabled(true);web.getSettings().setAllowFileAccess(false);web.getSettings().setAllowContentAccess(false);web.getSettings().setBlockNetworkLoads(true);
    web.addJavascriptInterface(new ConsoleApi(),"ProtectedConsole");
    web.setWebViewClient(new WebViewClient(){
      public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request){return true;}
      public WebResourceResponse shouldInterceptRequest(WebView view,WebResourceRequest request){return new WebResourceResponse("text/plain","UTF-8",new ByteArrayInputStream(new byte[0]));}
      public void onPageFinished(WebView view,String url){try{field(fragment,"ttydStatus").setInt(fragment,2);view.removeCallbacks((Runnable)field(fragment,"ttydTimeoutRunnable").get(fragment));view.getClass().getMethod("mapCtrlKey").invoke(view);}catch(Exception ignored){}}
    });
    String html="<!doctype html><html><head><title>Protected Linux</title><meta name=viewport content='width=device-width,initial-scale=1,maximum-scale=1'><meta http-equiv=Content-Security-Policy content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; font-src data:; connect-src 'none'\"><style>"+textAsset("web/xterm.css")+"html,body{margin:0;height:100%;background:#101014;color:#ddd;overflow:hidden}#terminal{height:100%;padding:6px;box-sizing:border-box}</style><script>"+textAsset("web/xterm.js")+"</script></head><body><div id=terminal></div><script>window.term=new Terminal({cols:80,rows:24,fontSize:Math.max(7,Math.min(14,(innerWidth-16)/48.2)),scrollback:3000,theme:{background:'#101014'}});term.open(document.getElementById('terminal'));term.onData(s=>ProtectedConsole.write(s));addEventListener('resize',()=>{term.options.fontSize=Math.max(7,Math.min(14,(innerWidth-16)/48.2));});window.acceptBytes=s=>term.write(Uint8Array.from(atob(s),c=>c.charCodeAt(0)));ProtectedConsole.ready();</script></body></html>";
    web.loadDataWithBaseURL("https://protected-linux.invalid/",html,"text/html","UTF-8",null);
  }
  public final class ConsoleApi {
    @JavascriptInterface public synchronized void ready(){
      if(closed||channel!=null)return;
      channel=new VmChannel(new VmChannel.Listener(){
        public void bytes(byte[] data)throws InterruptedException{emit(data);}
        public void ended(String error){try{emit(("\r\n["+(error==null?"シェルが終了しました。タブを閉じて開き直せます。":error)+"]\r\n").getBytes(StandardCharsets.UTF_8));}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}}
      });try{emit("保護VMを準備しています。初回は専用ディスクのコピーに時間がかかります…\r\n".getBytes(StandardCharsets.UTF_8));}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}channel.open('R');
    }
    @JavascriptInterface public void write(String data){if(data==null||data.length()>65536||closed)return;VmChannel current=channel;if(current!=null)current.write(data.getBytes(StandardCharsets.UTF_8));}
  }
  private void emit(byte[] data)throws InterruptedException{
    if(closed)return;outputSlots.acquire();WebView current=web;
    if(current==null||closed){outputSlots.release();return;}
    String script="acceptBytes('"+Base64.encodeToString(data,Base64.NO_WRAP)+"')";
    current.post(()->{if(!closed)current.evaluateJavascript(script,result->outputSlots.release());else outputSlots.release();});
  }
  private void shutdown(){closed=true;VmChannel current=channel;if(current!=null)current.close();web=null;fragment=null;android.util.Log.i("ProtectedLinux","CONSOLE_CLOSING");}
}
