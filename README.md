# Protected Linux Bridge

Android標準の「ターミナル」から、Snapdragon / Gunyahの保護VM上でDebianを使う実験用アプリ・Vectorモジュールです。独立したゲストカーネルで動作し、一時rootを利用します。

**このブランチはAndroid 17への移植版です。ROMの静的解析とビルドを進めていますが、Android 17の実機動作は未検証です。**

Android 16の最終版は、[v0.4.0-preview.9](https://github.com/ryuya0124/protected-linux-bridge/releases/tag/v0.4.0-preview.9)と固定ブランチ[android16-frozen](https://github.com/ryuya0124/protected-linux-bridge/tree/android16-frozen)に残しています。Android 17の変更は入りません。

## Android 17版の内容

- Galaxy S26 Ultra **SM-S948Q / S948QOPU1BZI9 / Terminal 37**専用の互換性チェック
- 標準ターミナルへの接続、Debianの複数タブ・専用ディスクを引き継ぐ移植コード
- Material 3の「ホーム」「画面」「連携」「設定」
- 設定から開ける**VMファイルのストレージ画面**：VMMなどのランタイム、ゲストカーネル、Debian／Androidディスク、実験ファイルの使用量を分類して表示
- ファイルサイズと、実際のディスク上の割り当て量を別々に表示
- 既存のディスク拡張・縮小、圧縮、復元機能への入口を維持

ストレージ一覧はrootを要求しますが、Vectorの有効化やVM起動は必要ありません。読み取り不可の項目は「一部未取得」と表示します。稼働中のVMを止めたり、既存ディスク・カーネル・バックアップを自動削除したりしません。

## 移植中の機能

DebianのGUI、GPU、通信、音声などのコードは残していますが、このホストOSでは実機未検証です。Android 16での検証結果を17版の保証として扱わないでください。

**Androidゲストの起動は現在停止しています。** Android 17のVMM／描画構成が変わっており、16版用GPUランタイムは同梱していません。既存のAndroidゲストディスクは削除・変換しません。Vulkanの実験経路も無効のままです。[移植状況と確認事項](docs/android17-port.md)を参照してください。

## 必要な環境

- 上記ビルドのGalaxy S26 Ultra、日本向けSM-S948Q
- `su -M`を使えるroot環境、Gunyahデバイス、標準のVirtualization APEX
- Vector / libxposed API 102（標準ターミナルとの接続に使用）
- Terminal versionCode 37と、そのインストーラーが用意したLinuxイメージ
- 主ユーザー（user 0）

未知のROM・Terminal・カーネル・VMMでは起動を拒否します。対応表への一致は実機での起動成功や安定性を保証しません。

## 導入と確認

1. Android 17版の署名済みrelease APKを使用します。Android 16にはインストールできません。
2. Bridgeでrootを許可します。「標準ターミナルを有効化」「Linuxインストール画面を開く」で準備します。
3. Vectorの適用先は**ターミナル (`com.android.virtualization.terminal`) だけ**にします。
4. ターミナルを開いて接続を確認します。起動不能時は繰り返し起動せず、`ProtectedLinux`ログと互換性情報を確認してください。
5. 容量の確認は、Bridgeの「設定」→「VMファイルのストレージ」から行えます。

端末は再起動しません。復旧時はBridgeの接続スイッチとVectorモジュールを無効化し、ターミナルを閉じます。データを保持する場合はアプリをアンインストールしたり、ストレージを消去したりしないでください。

## ホストの保護

**BLU、ホストカーネル・起動／システム領域の書き換え、ホストモジュール追加、SELinux変更、AVB／Gunyah保護の変更、Android再起動は行いません。** 追加カーネルはアプリ専用領域に置くゲスト用です。[禁止操作と境界](docs/host-boundary.md)を参照してください。

保護VMの起動でホストやハイパーバイザーの不具合が発生する可能性はあります。また、この経路はpvmfwによる検証済み起動・アテステーションを提供しません。

## 開発・ライセンス

[開発・ビルド手順](docs/development.md)、[Android 17の解析記録](docs/android17-port.md)、[ソースの整形方針](docs/code-style.md)。16版の詳細な実機記録は固定ブランチで参照できます。

アプリは[MIT License](LICENSE)です。依存ライブラリのライセンスはアプリの「ライセンス」と`third_party/`に掲載しています。ROM、OSイメージ、署名鍵、私的な実機ログは公開しません。
