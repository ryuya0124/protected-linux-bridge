package net.ryuya_dev.protectedlinux;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.*;

/** An authenticated raw stream to a guest PTY or VNC channel; never a host shell. */
final class VmChannel {
  interface Listener { void bytes(byte[] data) throws InterruptedException; void ended(String error); }
  private final ExecutorService reader=Executors.newSingleThreadExecutor();
  private final ExecutorService writer=new ThreadPoolExecutor(1,1,0,TimeUnit.MILLISECONDS,new ArrayBlockingQueue<>(32));
  private final Listener listener;
  private volatile LocalSocket socket;
  private volatile boolean closed;
  VmChannel(Listener listener){this.listener=listener;}
  void open(char kind){
    if(closed)return;
    reader.submit(()->{
      String error=null;LocalSocket current=new LocalSocket();
      try {
        if(!HookEntry.configReady())throw new IOException("Vectorとの設定通信が未準備です。Protected Linux Bridgeを開いてください。");
        current.connect(new LocalSocketAddress("net.ryuya_dev.protectedlinux.console",LocalSocketAddress.Namespace.ABSTRACT));
        if(current.getPeerCredentials().getUid()!=0)throw new IOException("起動支援の接続先を確認できません。");
        socket=current;if(closed)return;
        current.setSoTimeout(90000);current.getOutputStream().write(kind);int status=current.getInputStream().read();
        if(status!=0)throw new IOException(switch(status){case 2->"GUIが未導入です。ターミナルで plb-install-gui を実行してください。";case 4->"開けるセッション数の上限です。";case 6->"標準Linuxイメージが更新されました。既存データを保護するため停止しました。";case 7->"標準Linuxイメージがありません。アプリから標準インストーラーを開いてください。";case 8->"利用可能メモリが不足しています。";case 10->"専用Linuxディスクを準備する空き容量が不足しています。";case 9->"VM専用の通信ポートを準備できません。";default->"VMの準備が完了しませんでした。起動支援を確認してください。";});
        current.setSoTimeout(0);android.util.Log.i("ProtectedLinux","CHANNEL_CONNECTED "+kind);
        byte[] buffer=new byte[16384];int n;
        while(!closed&&(n=current.getInputStream().read(buffer))>=0)if(n>0)listener.bytes(Arrays.copyOf(buffer,n));
      }catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
      catch(Exception failure){if(!closed){error=failure instanceof IOException?failure.getMessage():failure.getClass().getSimpleName();android.util.Log.w("ProtectedLinux","CHANNEL_FAILED "+failure.getClass().getSimpleName());}}
      finally{disconnect(current);socket=null;if(!closed)listener.ended(error);}
    });
  }
  void write(byte[] data){
    if(closed||data.length>65536)return;
    try {writer.submit(()->{LocalSocket current=socket;if(current==null||closed)return;try{current.getOutputStream().write(data);}catch(IOException error){disconnect(current);}});}
    catch(RejectedExecutionException full){disconnect(socket);}
  }
  private static void disconnect(LocalSocket current){if(current==null)return;try{current.shutdownOutput();}catch(IOException ignored){}try{current.shutdownInput();}catch(IOException ignored){}try{current.close();}catch(IOException ignored){}}
  void close(){closed=true;disconnect(socket);reader.shutdownNow();writer.shutdownNow();}
}
