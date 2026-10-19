# Android VMのメモリ安定性調査

更新日: 2026-10-20。**4 GiBで起動できることと、アプリの導入・描画が安定することは別です。** 最新のガルパAPK転送ではGunyahのマッピング失敗が再発しました。ガルパは未導入、Aurora Storeの通信原因も未確定です。Vulkan試験のホストpanicを含む[経過](android-vulkan-investigation.md)とは、失敗段階を区別します。

## 保存ログの比較で分かったこと

[オフライン比較ツール](../tools/analyze_preallocated_backing.py)を追加し、以前成功したLinux検査と、今回のAndroid試験のRAM準備を比較しました。端末へ接続せず、メモリ確保もしません。ピン留め前後などの同一バッチ再観測は重複加算せず、欠落・不正なカウンター・複数試験の混在を検出します。

| 試験 | 設定RAM | 準備時THP被覆率 | compound head観測数 | 1 GiB当たりhead数 | 結果 |
| --- | --- | --- | --- | --- | --- |
| Linux全領域検査1 | 4480 MiB | 100% | 124525 | 28463 | 4096 MiBを全バイト検査、成功 |
| Linux全領域検査2 | 4480 MiB | 100% | 128431 | 29356 | 同検査を再度成功 |
| Android通常GLES、APK転送 | 4096 MiB | 100% | 181413 | 45353 | 起動後、Gunyah PAGE_FAULT -12 |

THPに含まれる観測容量をhead数で割った値も、成功例は約35.7〜36.8 KiB/head、失敗例は約23.1 KiB/headでした。**THP 100%でも、小さいfolioの多さを隠せます。** 全体の容量とTHP率だけでは同じメモリ条件とは言えません。

ただし、この表はGunyahのmapping数やEL2の使用量を測ったものではありません。準備時のkpageflagsの非原子的な観測で、貸与時まで物理配置が維持される保証もありません。LinuxとAndroidでは、ゲスト・VMM機能・共有I/O領域・ディスクアクセスも違います。この3件だけから成功するhead上限を決めたり、folio数だけを根本原因と断定したりしません。

[数値の集約データ](data/android-memory-stability-2026-10-20.json)を公開します。生ログや物理アドレスは含めません。

```sh
python3 tools/analyze_preallocated_backing.py /private/path/to/one-trial.log
```

## 今回はどこで失敗したか

ホストkernelログは、同じ失敗箇所について次を記録しています。

```text
Failed to map guest address: 10
Failed to provide folio: -12
```

VMM側もPAGE_FAULT attempt=-12でした。既存の[ソース・実カーネル解析](gunyah-root-cause-review.md)に基づくと、これは通常のLinuxページ確保失敗とは区別するべき、Gunyahへのアドレスマッピング段階です。どのEL2資源が尽きたかは未特定です。ホストの空きRAMを増やせば必ず解決するとは言えません。

[関連Linux実装](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/drivers/virt/gunyah/vm_mgr_mem.c)はfolioの実サイズを使って貸与・mappingを行います。[公開hypervisorのsparse mapping実装](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/mem/memextent_sparse/src/memextent_sparse.c)では、物理範囲の記録と実際のmappingの双方に資源が必要です。Samsung EL2と同一だとは確認できていません。単一の固定ページテーブル上限と決めつけない方針を維持します。

[SM8850の外部報告 #44](https://github.com/quic/gunyah-hypervisor/issues/44)も2026-10-20時点でOpenのままで、空きRAMが多い状態の同種エラーを報告しています。これは比較事例で、今回と同じ原因・リークの証明や公開済み修正ではありません。

## 安定性の改善に向けた順序

1. **準備の品質を測る。** 現在のpreallocated helperの観測成功は、必要なページが読み取れたことを確認しています。THP率やfolio分布を根拠付きで評価する安定性判定ではありません。次の計測設計では16〜512 KiBの分布、範囲ごとの偏り、再観測時の変化を区別します。生PFNを公開したり、貸与後の保護ページをホストから読み直したりしません。
2. **貸与とmappingを分ける。** 準備時head数をmapping数と見なさず、既知のエラードメイン・段階を記録します。vendorが公開していないheap診断や未知のhypercallを試しません。
3. **実際の利用負荷を段階化する。** 新たな実機合意を得た後に、起動、短い通信、少量のファイル転送、APK導入、アプリ描画、アイドルと正常停止を別々に検証します。起動確認だけを安定版の根拠にしません。停止後の資源回収も、プロセスが消えたことだけでは保証しません。
4. **許可された仕組みの中で比較する。** 同じmemfdの受け渡し、サイズ/seal/offsetの確認、通常LEND、既存のRAM予算・健康検査・復元監視は維持します。folioの品質から安全な上限を選ぶ方法や、ゲスト内のメモリ使用量を抑える構成を検討します。閾値や有効性をまだ実測していないため、今回は自動設定や新しい拒否基準をAPKへ追加しません。

マッピング失敗の自動再試行、意図的なVMMクラッシュ、mapped hole punchingの復活、未許可のTHP/compaction/cache-drop変更、host module/SELinux/保護の変更はしません。MADV_COLLAPSEやpin/unpinも、以前の失敗例を無視して復活させません。Androidの再起動を自動回復処理に組み込みません。

## APK転送時の砂嵐表示

ユーザーから「APK転送時にアプリ画面が砂嵐になり、ホーム画面は正常」と報告されました。表示を確認した再現動画・同一時刻の画素証跡はまだ取得していません。通信エラーやゲームのVM検出と同一視しません。

共有DMA/SWIOTLBの同期、バッファ所有権・寿命、サイズ/stride、GPU資源再利用を比較対象にします。現行の別ゲストカーネルには既に[guest-bound transferの完了時同期backport](../kernels/android-gpu/guest-bound-dma-sync.patch)がありますが、あらゆる方向・描画経路の正しさを証明するものではありません。提出直後の同期追加、fenceの無視、サイズの偽装、PNGへの黙った切り替えで隠しません。

メモリ不足と画素破損が同じ原因かも未確定です。今後の許可済み試験では、転送前後の同じ画面・resource/fenceの状態・Gunyah失敗時刻を対応付ける必要があります。現在は許可された起動回数を消費済みなので、追加実機負荷は停止し、保存ログとソースの解析に限定しています。
