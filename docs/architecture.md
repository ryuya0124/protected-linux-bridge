# 拡張機能の構成と判断

2026-10-18に実装・実機確認。初期の読み取り専用UART版はコミット `162a71c` に保存しています。現在の機能と制限はREADME、実際に確認した範囲はdevice-validationを参照してください。

## 既存カーネルでできる範囲

端末内のMicrodroidカーネルではNETDEVICES、DRM、framebuffer、overlayfsが無効ですが、virtio-vsock、virtio-blk、PTY、IPv4 loopbackは有効でした。そのためカーネルを交換せず、以下のユーザー空間経路を実装しています。

```text
標準Terminal（Vector API 102）
  ├─ タブ別xterm.js → 認証付きUnixソケット → vsock → PTY/bash
  └─ noVNC           → 同じUnixソケット      → vsock → ゲストlocalhostのTigerVNC

ゲストapt/curl → localhost:3128 → vsock → Terminal UIDのHTTPプロキシ → 接続先
```

## ディスク

初回に標準Terminalの `root_part` をモジュールのプライベート領域へ読み取り専用でコピーし、そのコピーをbackingとするqcow2ディスクを作成します。元のイメージは書き込みません。標準インストーラーの更新と、こちらの保存データを分離します。

元イメージの所有UID・種類・サイズ、コピー前後のstat、空き容量を確認します。コピーの途中状態は `.pending`、完了後はrenameで公開します。専用baseとqcow2の対応が崩れた場合は、既存データを上書きせず停止します。ライブ状態のqcow2ファイルをコピーしてバックアップしたと主張してはいけません。

## セッションと停止

独自の静的リンクしたmusl製agentが、選択によりゲストPID1またはsystemd配下のサービスとして動作し、PTYとlocalhostプロキシ、VNCへの接続を管理します。これはゲストのユーザー空間であり、カーネルのビルドではありません。

1つのVMへ最大12のシェル／GUI接続を許可します。各シェルは独立したPTYです。常駐を選択しない場合の最後の対話接続終了、親プロセス終了、明示停止、設定した時間制限でVMを終了します。まずゲストをsyncして停止し、期限内に止まらない場合だけ追跡した自分のcrosvmへシグナルを送ります。

起動支援はnativeソースとビルドスクリプト（GPU配布物の固定ハッシュを含む）の識別子を確認します。更新で一致しなくなった場合、古い支援を停止してから新しいものを起動します。実行中バイナリの上書きを避け、展開も別inodeへのrenameを使用します。

## ネットワーク

ホスト側のリスナーはvsockのみで、VMのCIDだけを受け付けます。実通信はrootからTerminalのUIDへ降格した子プロセスが実行し、補助グループはinetのみ、capabilityはなく、no_new_privsを設定します。AndroidのTerminal向け通信制限とVPN設定に従います。

HTTP forwardとHTTPS CONNECTを提供し、TLSを復号しません。名前解決はAndroidの既存リゾルバーです。ホスト自身のアドレス、loopback、link-local、multicastなどを拒否し、検査できない場合は許可しません。一般のLAN上の別ホストは、この禁止範囲には含みません。

ホストのNIC・route・DNS・iptables/nftables・VPN設定には書き込みません。仮想NIC相当ではなく、対応ツールが使う明示プロキシです。UDP用の明示転送とIPv4 echoを同じvsockで扱います。先頭NULのバージョン付きヘッダーでHTTPと区別し、長さ付きレコードでゼロ長を含むUDPの境界を保ちます。宛先ごとのconnected UDPを使い、受信元のすり替えを防ぎます。pingは `SOCK_DGRAM/IPPROTO_ICMP` で、rootやCAP_NET_RAWを使いません。echo requestの種類・checksum・サイズ・頻度・回数を制限します。ゲストもsequenceとnonce/payloadを照合します。

レコードの上限は65507 bytes、受信途中の切断・過大長は接続を終了します。ホスト側はHTTPを含め最大16ワーカーです。UDPは60秒無通信・30分の期限、pingは最大64回です。透明なIPトンネル・着信待受・IPv6 echoはありません。

調査中、モジュールUIDでの通信がDozeとバックグラウンド制限により拒否されました。ポリシーを解除するのではなく、実際に操作中のTerminal UIDに通信を割り当てる構成へ変更しました。画面OFFやバックグラウンドの維持は別問題であり、この版で保証していません。

## GUI

TigerVNC / xterm / OpenboxをVM内で任意にインストールします。ゲストのVNCはlocalhostのみ、X11のTCPは無効です。無認証VNCをホストやLANのTCPポートに公開しません。

ホスト画面は未改変のnoVNC 1.6.0 ESモジュールを使用し、raw channelを認証済みUnix/vsockストリームへ接続します。WebViewの外部ネットワーク・ファイル・content・ナビゲーションを禁止し、固定の仮想HTTPS originに同梱ソースだけを供給します。入力／出力キューとチャンク数を制限します。対応するWebViewではAndroidX WebKitのArrayBufferメッセージを使い、Base64変換とevaluateJavascriptの文字列処理を省きます。固定origin・main frameのみのreadyメッセージを受け、表示の消費通知ごとに上限8チャンク（各最大64 KiB）の枠を解放します。未対応時は従来の文字列方式を使います。Dialogのハードウェア合成を要求し、実際の状態をログに出します。VNCの符号化そのものがGPU化されるわけではありません。初回のデスクトップ描画用に接続後1回だけポインターを中央に移動（クリックなし）してフルフレームを要求し、周期ポーリングは行いません。ゲスト側のXスクリーンセーバーは無効にし、起動時に背景を設定して初期描画を確定します。

モバイルIMEは下書き文字列を維持し、「送信」「Enter」のタップで送ります。常時逐次送信ではIMEの変換と空白入力が不安定だったためです。ゲスト内で操作対象のウィンドウをタップしてフォーカスを与える必要があります。音声、全キーボードや複雑なデスクトップ環境は検証対象外です。

## OpenGLのGPU転送

```text
ゲストMesa virpipe → guest Unix VTest socket → guest-side VTest v0 negotiation
                  → vsock（CID認証・最大4接続）→ 不透明なバイト転送
                  → Unix socket（UID/PID照合）→ nobodyのvirglrenderer
                  → Android EGL / GLES → Adreno GPU
```

既存ゲストカーネルにDRMがないためvirtio-gpu/Venusのデバイスを追加しません。Mesaのユーザー空間VTest経路を利用します。FDや共有メモリーを渡すVTest v2/v3はVM境界を越えられないため、ゲスト側で対応済みのv0を交渉します。初期ハンドシェイクが変わった場合は切断し、未知バージョンを許可しません。GPU命令の解釈はrootでは行いません。

Termux配布の固定バージョンvirglrenderer-android 1.3.0-1を改変せず使用し、SHA256をビルド時に検証します。コードのみをroot所有の `/data/local/tmp/net.ryuya_dev.protectedlinux.gpu` へ展開します。ユーザーデータは置きません。公開ディレクトリーと入力はsymlink・種類・所有者を確認し、ファイルは別inodeで置換します。

デコーダーは補助グループなし・UID/GID 9999・capabilityなし・no_new_privsで起動し、外部にTCPポートを開きません。root中継は接続元CID、接続先UnixソケットのUIDと追跡したserver PIDを照合します。非rootの監視プロセスを専用プロセスグループの先頭に置き、親終了・server終了・期限切れでフォークされたrenderer群も終了します。root親は元の子PIDをreapする前にそのグループを停止します。

これは完全なGPUサンドボックスやGPUの専有ではありません。同じホストGPUドライバーを使い、その脆弱性の影響も受け得ます。nobodyへの降格と資源上限は、悪意ある描画データへの完全な防御を証明するものではありません。

`plb-install-gpu` はVM内のMesaを取得してモードファイルを作ります。次回VM作成時、シェルとGUIへ `GALLIUM_DRIVER=virpipe` / `VTEST_SOCKET_NAME` を渡します。`LIBGL_ALWAYS_SOFTWARE=1` はMesaのloaderにDRMデバイスを使わせないためであり、virpipeが選ばれたときの実描画先はホストGPUです。未導入時は従来のCPU描画です。`plb-gpu-mode off` とVM再作成で戻せます。`plb-gpu-run` は個別プロセス用の明示指定です。

VTest v0の転送にはコピーが必要です。OpenGL呼び出しのGPU化であり、Xvnc・noVNC画面転送をゼロコピー化するものではありません。対応するGLバージョンを偽装する設定は使っていません。

### 2D合成とVulkanの制約

GPUインストーラーはWestonとXwaylandもゲストに用意し、次回起動時にWeston X11バックエンドをGL rendererで選択した解像度（初期値1280×720）のXvnc出力上へ表示します。デスクトップ内のウィンドウ合成はVirGL経由でAdrenoへ送り、Xvncは最終画面の取得とRFB転送に使います。Westonの起動に失敗した場合は従来のOpenbox/xtermへ進みます。`plb-desktop-mode classic|gpu` で次回VMのデスクトップを選択できます。GPUモードをオフにすると従来デスクトップを使います。モードファイルはsyncして保存します。

これはすべての2Dアプリの内部描画がGPUになるという意味ではありません。Waylandのwl_shmアプリはCPUで作った内容をGPUで合成し、VNC取得・符号化は引き続きCPUを使います。DMA-BUF、共有メモリーによるVM間ゼロコピーは提供していません。

Mesa VenusのVTest実装は `SCM_RIGHTS` で受けたresource FDをmmapし、リング／共有メモリーとして使います。通常のvsockバイト転送ではホストとゲストのFDやページは共有できず、OpenGL用v0への交渉ではVulkanは動きません。既存ゲストにvirtio-gpu/DRMもありません。GPU VulkanにはMesa/Venus側の資源・同期・メモリー転送の追加実装が必要です。LavapipeはCPUであり、GPU Vulkan対応の証拠には使いません。カーネル交換で迂回することはありません。


## 直接GPU表示とBridgeの全画面GUI（0.3）

新しい表示経路はゲストのXvncからraw RGBXを読み、JavaScriptを通さずDirectByteBuffer→GLES3テクスチャ→Android Surfaceへ表示します。取得と境界コピーはCPUに残ります。RFBの圧縮／展開を省く代わりに帯域が増え、高解像度・高fpsは速くなる保証がありません。noVNC互換表示を保持しています。

Bridgeの非公開DesktopActivityも同じ認証付きGUI接続を使います。既存のTerminal PTYを閉じず、GUI終了はその接続だけ閉じます。最後の接続終了時は通常通りVMを停止します。物理キーマウの入力マッピングとフォーカス喪失時の解放を実装し、機器による確認は未実施です。

描画スイッチ、解像度ID、更新上限はホストのroot専用固定形式ファイルへ保存して、次回のVM起動時にゲストへ渡します。Androidのグローバル表示設定は変えません。画面転送方式は開き直しで反映します。詳しくは[実装記録](development.md#03-display-and-configuration)、[保護境界](security.md)を参照してください。

## 0.4の追加

音声・クリップボード・systemd・容量拡張・プロファイルの境界は [development.md](development.md#04-profiles-and-integrations) を参照してください。既存Microdroidと新しいゲストカーネルを区別します。[ゲストカーネル方針](guest-kernel-plan.md)の制約が優先し、ホストのカーネルや保護機構は変更しません。
