# Protected Linux Bridge

Android標準の「ターミナル」を、Snapdragon / Gunyah端末の保護VM上で動くDebianシェルへ接続する実験用モジュールです。一時rootとVectorを使い、端末に元からあるカーネルだけで起動します。

**カーネル更新、ブートローダーアンロック、パーティション書き換え、再起動、SELinux設定変更は行いません。**

## 現在できること

- 標準ターミナルの画面でDebian 13のコマンドを入力・実行
- Gunyahの保護VM経路を使用（Android上のchrootではありません）
- root権限の要求と起動支援の準備
- Vectorとの接続状態の表示、接続のオン／オフ
- ターミナル終了時のVM停止と、起動支援の待機中の再起動

これは標準ターミナルの完全な代替ではありません。Debianのルートディスクは**読み取り専用**、RAMは512 MiB、CPUは1コア、コンソールは1つです。ネットワーク、GUI、複数タブ、永続的な書き込み、標準のディスク設定には未対応です。一時ファイルはVM終了時に消えます。1セッションは最大30分、起動支援の待機は最大30分です。

`--protected-vm-without-firmware` を使い、pvmfwによる検証済み起動・アテステーションは提供しません。通常のAVFで検証されたゲストと同じ保証を持つ製品ではありません。

## 対応条件

実機で確認したのは **Galaxy S26 Ultra SM-S948Q / Android 16 / One UI 8.5 / 標準Terminal versionCode 36** のみです。他のSnapdragon端末での動作は保証しません。更新で契約が変わった場合、フックは適用を停止します。

- 既存のGunyah保護VM対応と既存のMicrodroidカーネル
- `su -M` が使える一時root環境
- Vector / libxposed API 102
- 標準ターミナルがインストールしたDebianイメージ
- 主ユーザー（user 0）。他ユーザーには未対応
- 起動時に1.5 GiB以上の利用可能メモリ

## 使い方

1. このアプリの「標準ターミナルを有効化」「Linuxインストール画面を開く」で標準機能を準備します。標準のインストーラーでLinuxイメージを取得します。起動アクティビティだけを有効にするとインストーラーを開けない場合があります。
2. Protected Linux Bridgeの署名済みrelease APKをインストールします。
3. Vectorでこのモジュールを有効にし、適用先に **ターミナル (`com.android.virtualization.terminal`) だけ**を選びます。Androidシステムは不要です。
4. Protected Linux Bridgeを開き、root要求を許可します。
5. 「標準ターミナルを開く」を押します。Debianのプロンプトが表示されます。

ターミナルが既に起動中なら閉じて開き直してください。端末の再起動は不要です。`exit` またはターミナル画面の終了でVMが止まります。起動支援が期限切れ、またはrootが失われた場合は、このアプリで準備し直してください。

## 無効化

アプリの接続スイッチをオフにし、ターミナルを閉じます。Vectorでもモジュールを無効にすれば標準動作へ戻ります。元の標準ターミナルが非保護VM非対応で落ちる端末では、オフにするだけでは標準機能が使えるようにはなりません。標準ターミナルのLinuxイメージは削除・改変しません。

[実装とビルド](docs/development.md) · [実機検証記録](docs/device-validation.md)

## ライセンス

このプロジェクトのコードはMITです。libxposed、AndroidX、Material、xterm.jsなどの依存ライセンスはアプリ内と `third_party/` に収録しています。Androidのカーネル、crosvm、Debianイメージ、標準ターミナルAPKは同梱していません。
