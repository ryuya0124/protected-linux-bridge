# AndroidゲストのVulkan調査

更新日: 2026-10-20。対象はSM-S948Q / Android 16 / Adreno 840上の、別プロファイルのAndroid 17ゲストです。

**Vulkanは未対応です。2回目のVM試験でホストAndroidがカーネルパニックを起こしました。新しいVM・GPU・メモリ負荷試験は停止しています。** 既存のGLES版APKへVulkanを有効化していません。再起動後の状態が正常でも、試験再開の根拠にはしません。

## 確認できた範囲

| 段階 | 結果 | この結果では確認できないこと |
| --- | --- | --- |
| ホストVulkanの小画像確保 | 通常、opaque FD指定、AHB export/importの確保・bind成功 | ゲスト描画、FDを介した実転送、安定性 |
| 修正gfxstreamの単独初期化 | Adreno 840で64×64 ColorBuffer確保・AHB export・初期化・終了成功 | VM経由のVulkan実行 |
| 別ディスクのVM試験1 | ゲストADB到達。認証済み表示クライアント不在で終了 | ゲストVulkan描画 |
| VM試験2 | rootゲストADB、ranchu選択。試験コマンドはBus error、その後ホスト再起動 | デバイス列挙、画素読み戻し、正常終了 |
| 再起動後の読み取り調査 | 保存されたホストpanicを確認。元ディスク4点のハッシュ一致 | 失敗試験の正常な後処理、最初のクラッシュ原因 |

ホストだけの成功をゲスト対応済みとは扱いません。CPUのSwiftShaderへ切り替えて成功とする処理もありません。

## 初期化を直した内容

対象は[AOSP gfxstream 7e9d595b097b23f5fa5c114088245c819e29a9af](https://android.googlesource.com/platform/hardware/google/gfxstream/+/7e9d595b097b23f5fa5c114088245c819e29a9af/)です。該当するVkCommonOperations.cppは、以前に保存した解析対象ソースとSHA256が一致します。インストール済みバイナリ全体の由来を証明するものではありません。

[公開差分](../tools/gfxstream_android_vulkan.patch)は次を変更します。

1. インスタンス作成などのグローバル関数4点を、VK_NULL_HANDLEのvkGetInstanceProcAddrから取得します。
2. Android Hardware Buffer（AHB）用画像のbind前にvkGetImageMemoryRequirementsを呼びません。この呼び出しは[VUID-image-04004](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetImageMemoryRequirements.html)に反します。
3. AHB exportは専用画像を指定してallocationSize=0で実際の確保をドライバーに委ね、importは実AHBのプロパティを使います。[AHB専用の割り当て規則](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryAllocateInfo.html)に基づく処理で、架空の必要容量を返すフックではありません。
4. AHB exportのhandle maskへDMA_BUFを混在させません。個別サポートだけでは組み合わせを保証できず、この端末では混在した経路の確保が失敗しました。
5. 未初期化のexport handle参照を避け、取得後のAHBから実容量・memoryTypeBitsを記録します。
6. Android向けCMakeのplatform source/link設定を追加します。

修正途中の単独初期化は2回SIGABRTでした。そのホストtombstoneは回収済みです。その後の修正版だけが初期化・終了に成功しました。失敗例も残します。

単独メモリ試験の結果:

| 経路 | 必要容量など | 確保 / bind |
| --- | --- | --- |
| 通常画像 | 20480 bytes、alignment 4096、type bits 1 | 成功 / 成功 |
| opaque FD指定画像 | 25780 bytes、type bits 3 | 成功 / 成功 |
| AHB export | 要求0、専用画像指定。実AHB 16384 bytes、bind後画像21896 bytes、type bits 2 | 成功 / 成功 |
| AHB import | 実AHB 16384 bytesを指定。bind後画像21896 bytes、type bits 2 | 成功 / 成功 |

この試験はVMを起動せず、明示的な描画コマンド投入やopaque FDの実exportもしません。画像とAHBの容量は別の問い合わせ結果なので、同じ値とは仮定しません。

## 分離したVM試験

- 停止中の元cf17-gpu4096から4ファイルをハッシュ確認してcf17-vulkan4096へコピーしました。Debianと元Androidディスクは接続しません。
- runtimeもandroid-vulkan4g-runtimeへ分離。RAM4096 MiBの内側に128 MiBの共有I/O領域を含めます。
- ゲストVulkan HALはranchu。full capsetとVulkanUseDedicatedAhbMemoryType:enabledを使いました。
- 修正rendererは所有VMMのLD_LIBRARY_PATHだけで選択。ホストライブラリ置換、LD_PRELOAD、メモリ/API/ioctlの偽装はしていません。
- protected LEND、共通owner lock、6 GiB予算、空きRAM下限、健康検査、6項目だけの一時shmem設定、40秒の独立復元監視は維持しました。

試験1は認証済み表示クライアント不在で終了しました。試験2用keepalive helperも正常接続の記録が取れず、表示クライアント問題が解消したとはしていません。

[ゲスト試験コード](../tools/android_guest_vulkan_probe.c)はゲスト種別・ビルド・UID・nonceを検証し、実GPUで2色の64×64画像を全画素読み戻す設計です。試験2ではデバイス名・読み戻し・完了マーカーを得られず、Bus errorだけでした。開始マーカーもないため、どのVulkan関数まで実行されたか、プログラム本体へ到達したかも確定できません。

## 再起動の保存ログ

ホストのpstoreは空でしたが、/proc/last_kmsgに次が残っていました。生ログ・識別子・レジスター値は公開しません。

```text
Comm: crash_dump64
Internal error: synchronous external abort
__arch_copy_to_user
copy_page_to_iter
process_vm_rw
__arm64_sys_process_vm_readv
...
Kernel panic - not syncing: synchronous external abort: Fatal exception
```

VM開始は07:47:24 JST、panicは同じ保存ログの時刻対応で約07:47:42 JSTです。手動再起動コマンドは送っていません。単なるADB切断ではなく、ホストカーネルの致命的な例外です。起動理由プロパティのrebootだけでは判別できなかった点です。

**確定したのは、クラッシュ収集プロセスが他プロセスのページを読み出す途中でホストが落ちたことです。** 対象PID・mapping・最初のシグナルについて、今回の時刻の完成したtombstoneはありません。ゲスト試験のBus errorをホストVMMのシグナルやpanic原因と同一視できません。

以下は静的解析の仮説・比較対象です。

- 保護VMへ貸与したページ、またはGPU/外部mappingへクラッシュ収集が触れた可能性。該当ページの帰属を証明する記録はありません。
- [Android commonのprocess_vm_access.c](https://android.googlesource.com/kernel/common/+/refs/heads/android16-6.12/mm/process_vm_access.c)は対象ページを取得してcopy_page_to_iterへ渡します。保存スタックと対応する参考実装ですが、Samsungビルドとの完全一致は未確認です。
- [libunwindstack](https://android.googlesource.com/platform/system/unwinding/+/refs/heads/android16-qpr2-release/libunwindstack/Memory.cpp)はprocess_vm_readvを使用し、[debuggerd](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-qpr2-release/debuggerd/libdebuggerd/tombstone_proto.cpp)はレジスター周辺のメモリも読みます。通常のcore dumpとは異なる明示的な読み出しです。
- [使用したcrosvm](https://android.googlesource.com/platform/external/crosvm/+/8a4abfbaea5567de24e49cc43874c820098987d0/)の通常mapping生成は既にMADV_DONTDUMPを要求します。同じフラグの追加だけでdebuggerdの明示的アクセスを防げるとは扱いません。
- Vulkanのhost-visible blobをVMMへmapする経路。ただし今回その分岐が実行された証拠はありません。

再現目的のVMMメモリ読み取り・デバッガー接続・意図的クラッシュはしません。debuggerdやホスト保護機構の無効化も解決策にしません。

再起動後はroot利用可能、taint 4608、SELinux Enforcing、6項目すべてneverでした。元のworking/misc/FRP/ramdiskは事前SHA256と一致しました。これは**再起動後の観察**であり、失敗VMの正常な後処理を確認したものではありません。実験コピーも正常終了済みとは扱いません。

### 通常GLES版でも別のメモリ障害を確認

その後、ユーザーが通常GLES版を起動し、Aurora Storeの通信問題とガルパ導入を依頼しました。既存VMの通信はAndroidからVALIDATEDと判定されていましたが、HTTPS実測は未完了です。日本版ガルパ10.2.0の正規署名付きbase/ARM64 splitをホストから読み取り、ゲストへの転送中、08:03:40 JSTにGunyah PAGE_FAULT attempt=-12でVMが終了しました。インストール成功とはしていません。この時点のホストboot IDは不変、taint 4608、Enforcing、6項目neverでした。Vulkan時のホストpanicとは別の失敗段階です。

ユーザーが許可した通常GLESの試行1回はSurface unavailableでVM起動前に終了しました。さらに明示的な許可を得た1回はBOOT=1/rootゲストADBへ到達しましたが、08:08:26 JSTにAPK転送開始時のGunyah PAGE_FAULT attempt=-12で再び終了しました。ホストboot ID・taint・Enforcing・6項目neverは維持されました。2つの許可は消費済みで、追加の自動再起動はしません。

ガルパは未導入、HTTPS実測とAuroraの接続原因の特定も未完了です。通信のVALIDATED表示をAuroraの認証・API接続成功の証拠にはしません。新しい[固定宛先のHTTPS調査コード](../tools/android_guest_network/GuestConnectivityProbe.java)は通常の証明書検証とタイムアウトを維持しますが、今回のVMでは実行結果を得られませんでした。ゲームのセーブ・アカウント情報はコピーしていません。

## 実行禁止と調査用ソース

Vulkanビルドのmanager、policy helper、RAM proxyはmain入口でexit 43を返します。--checkでもRAM確保・ファイル操作・ホスト設定変更より前に拒否します。NDKビルドと入口逆アセンブルでログ出力・exit 43だけになることを確認しました。通常GLES用ビルドへVulkan分岐は混ぜません。

ビルドスクリプトは端末に接続せず、APK実行メニューも追加しません。

- [rendererビルド](../tools/build_android_vulkan_renderer.py): source revision、差分、platform入力SHA256を照合。
- [実行禁止のVM成果物ビルド](../tools/build_android_vulkan_trial.py): VMM・kernel・initramfs・renderer・元ディスクを個別照合。
- [読み取り専用ログ回収](../tools/collect_android_vulkan_failure.py): 指定Galaxyの型番確認後、失敗・タイムアウトでもstdout/stderrを保存。VM起動・再起動・設定変更・ログ消去はしません。

ソースと差分を公開しますが、失敗構成のバイナリやOSイメージをAPKへ含めません。

### Mac上だけでrendererをビルドする

専用checkoutを上記gfxstream revisionへ合わせます。NDK 28.2.13676358、ARM64 API 35、CMake、Ninjaを使用。約71 MiBの浅いcheckoutで調査し、AOSP全体は取得しませんでした。

platform-headersにはAndroid 16 QPR2のcutils/native_handle.hとvndk/hardware_buffer.h、platform-libsには解析端末から読み取ったlibnativewindow.soとlibcutils.soを置きます。スクリプトの固定ハッシュと不一致なら停止します。リンク入力は再配布しません。

```sh
python3 tools/build_android_vulkan_renderer.py \
    --sdk /path/to/android-sdk \
    --source /path/to/dedicated-gfxstream-checkout \
    --platform-headers /path/to/reviewed-platform-headers \
    --platform-libs /path/to/reviewed-platform-libs
```

単独初期化に成功したrenderer SHA256:

```text
d5343369aa839328bf6506282ad843f8bdc0e7248cde24505124246149cf57f5
```

適用差分SHA256:

```text
0a2257e6bbf5bd8dd827523da1520ad250683cbad3e6ce310f3124204f0fa008
```

別入力・差分・ビルドへ、この検証結果は流用しません。ログ回収も接続先を明示します。

```sh
python3 tools/collect_android_vulkan_failure.py \
    --adb /path/to/adb --adb-port 5038 --serial GALAXY_IP:PORT
```

生ログ、tombstone、識別子、ディスク、署名情報、生成バイナリはignored artifacts内だけに残します。

### 今回のコード検証

Python/nativeの198テストが通過しました。ログ回収のテストには、別端末の拒否、root拒否時の失敗出力保持、タイムアウト途中のkernelログ保持を含みます。オフラインRAM比較も重複・欠落・不正な観測を検査します。4種類のC試験コードはNDKのWall/Wextra/Werror付きビルドが通り、実行禁止にしたmanager・policy・RAM proxyも再ビルドしました。これらはMac上のコード確認です。別途行った通常GLESの許可済み再試行は上記のとおり失敗しており、Vulkanの再試験や有効化APKの更新・リリースはしていません。

## 再開前に必要なこと

1. 最初の異常と、クラッシュ収集でホストが落ちる経路を分離する静的解析。
2. 保護ページや外部mappingへ収集処理が触れない設計のレビュー。ホスト設定・保護の無効化で回避しないこと。
3. 根拠を説明した上で改めて明示的な実機試験の合意。新しいbootの正常taintだけでは再開しないこと。
4. その後に実GPU名、nonce付き2色全画素チェック、正常停止、ポリシー復元、元ディスク・ホスト境界の確認。揃うまで対応済みと表示しないこと。
