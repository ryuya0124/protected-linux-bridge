package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.app.Dialog;
import android.util.Base64;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.Semaphore;

/** In-process software display. Assets and guest bytes never require a host TCP listener. */
final class DisplayController {
  private static final Map<Activity,DisplayController> active=new WeakHashMap<>();
  private final Semaphore frames=new Semaphore(8);
  private Dialog dialog;private WebView web;private VmChannel channel;
  private volatile boolean closed;
  static void open(Activity activity,String apk){if(active.containsKey(activity))return;DisplayController controller=new DisplayController();active.put(activity,controller);try{controller.show(activity,apk);}catch(Exception error){active.remove(activity);controller.shutdown();android.util.Log.w("ProtectedLinux","DISPLAY_SETUP_FAILED "+error.getClass().getSimpleName());}}
  static void close(Activity activity){DisplayController controller=active.remove(activity);if(controller!=null){controller.shutdown();if(controller.dialog!=null)controller.dialog.dismiss();}}
  private void show(Activity activity,String apk)throws IOException{
    dialog=new Dialog(activity);dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);web=new WebView(activity);dialog.setContentView(web);
    web.getSettings().setJavaScriptEnabled(true);web.getSettings().setAllowFileAccess(false);web.getSettings().setAllowContentAccess(false);web.getSettings().setBlockNetworkLoads(true);
    web.addJavascriptInterface(new DisplayApi(),"ProtectedDisplay");
    web.setWebViewClient(new WebViewClient(){
      public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request){return true;}
      public WebResourceResponse shouldInterceptRequest(WebView view,WebResourceRequest request){
        var uri=request.getUrl();String path=uri.getPath();
        if("https".equals(uri.getScheme())&&"protected-linux.invalid".equals(uri.getHost())&&path!=null&&path.startsWith("/novnc/")&&!path.contains("..")){
          try{return new WebResourceResponse(path.endsWith(".js")?"application/javascript":"text/plain","UTF-8",new ByteArrayInputStream(Controller.asset(apk,path.substring(1))));}catch(IOException ignored){}
        }
        return new WebResourceResponse("text/plain","UTF-8",new ByteArrayInputStream(new byte[0]));
      }
      public boolean onRenderProcessGone(WebView view,RenderProcessGoneDetail detail){activity.runOnUiThread(()->close(activity));return true;}
    });
    web.setWebChromeClient(new WebChromeClient(){public boolean onConsoleMessage(ConsoleMessage message){if(message.messageLevel()==ConsoleMessage.MessageLevel.ERROR)android.util.Log.w("ProtectedLinux","VNC_JS_ERROR line="+message.lineNumber()+" "+message.message().substring(0,Math.min(200,message.message().length())));return true;}});
    dialog.setOnDismissListener(ignored->{active.remove(activity);shutdown();});dialog.show();dialog.getWindow().setLayout(WindowManager.LayoutParams.MATCH_PARENT,WindowManager.LayoutParams.MATCH_PARENT);dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    web.loadDataWithBaseURL("https://protected-linux.invalid/",new String(Controller.asset(apk,"web/display.html"),StandardCharsets.UTF_8),"text/html","UTF-8",null);
  }
  public final class DisplayApi {
    @JavascriptInterface public synchronized void ready(){if(closed||channel!=null)return;channel=new VmChannel(new VmChannel.Listener(){public void bytes(byte[] data)throws InterruptedException{emit("feedVnc",data);}public void ended(String error){try{emit("displayEnded",(error==null?"画面の接続が終了しました。":error).getBytes(StandardCharsets.UTF_8));}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}}});channel.open('V');}
    @JavascriptInterface public void write(String encoded){if(encoded==null||encoded.length()>90000||closed)return;try{byte[] data=Base64.decode(encoded,Base64.NO_WRAP);VmChannel current=channel;if(current!=null)current.write(data);}catch(IllegalArgumentException ignored){}}
    @JavascriptInterface public void finish(){VmChannel current=channel;if(current!=null)current.close();}
  }
  private void emit(String method,byte[] bytes)throws InterruptedException{if(closed)return;frames.acquire();WebView view=web;if(view==null||closed){frames.release();return;}String script=method+"('"+Base64.encodeToString(bytes,Base64.NO_WRAP)+"')";view.post(()->{if(!closed)view.evaluateJavascript(script,result->{view.postInvalidateOnAnimation();frames.release();});else frames.release();});}
  private void shutdown(){closed=true;VmChannel current=channel;if(current!=null)current.close();WebView view=web;web=null;if(view!=null){view.removeJavascriptInterface("ProtectedDisplay");view.stopLoading();view.destroy();}}
}
