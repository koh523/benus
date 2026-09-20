# blenus/benus

**日本語** | [English](README.en.md)

blenus は、Nordic UART Service（NUS）over BLE を、μT-Kernel 3.0（micro:bit v2）から `tk_*_dev` で扱えるデバイスドライバです。  
benus は blenus を micro:bit 用 μT-Kernel 3.0 へ適用させるパッケージです。

## 背景

micro:bit でよく使われる Microsoft MakeCode では、BLE による無線通信が標準で使えます。一方、パーソナルメディア株式会社（以降は PMC と表記）から提供されている micro:bit 向け μT-Kernel 3.0 では、外部との通信手段は USB シリアルに限られます。

μT-Kernel 上でも無線が使えれば、PC にケーブルでつながずともタスクの状態をやり取りしたり、センサ値を送ったりできます。micro:bit を μT-Kernel の実行環境として使う場面が広がり、その利用価値も上がります。

ただし BLE のプロトコルスタックは、Nordic 社が SoftDevice としてバイナリで提供するものです。API は公開されていますが、実装はブラックボックスです。呼び出しは SVC（スーパーバイザコール）経由に限られ、μT-Kernel のアプリケーションから直接扱うにはハードルが高いです。

blenus は、その SoftDevice と Nordic UART Service（NUS）を μT-Kernel のデバイスドライバとしてまとめ、`tk_opn_dev` / `tk_swri_dev` などの既存のデバイス API から使えるようにしたものです。アプリケーションは BLE スタックの詳細を意識せず、シリアルに近い感覚で無線通信を扱えます。

本リポジトリ（benus）はドライバと SoftDevice 周辺、ホストツリーへのパッチ、利用例（`app_sample`）、動作確認用 Web アプリ（`web/`）を含みます。PMC 配布の μT-Kernel（micro:bit 依存を含むカーネル本体）は同梱しません。先に PMC の `mtkernel_3` を展開し、本リポジトリの成果物をそこに配置してパッチを当ててビルドします。

## 前提条件

対象ボードは **micro:bit v2** です。v1 では動作しません。  
ホスト PC には、[「micro:bitでμT-Kernel 3.0を動かそう」第2回（開発ツールの準備とコンパイル）](https://www.t-engine4u.com/info/mbit/2.html) が示す開発ツールが入っていることを前提とします。GNU Arm Embedded Toolchain、make（Windows では xPack Windows Build Tools など）、Python 3.8 以降が必要です。Eclipse は任意で、`build_make` からのコマンドライン構築でも構いません。  
また、本手順書は、Git Bash 環境下での操作を想定しています。

## セットアップ（PMC 展開 → clone → 配置とパッチ）

作業用ディレクトリは空の場所を選んでください。PMC の `mtkernel_3` がビルドのルートです。本リポジトリはそれとは別に clone します。

### 1. PMC の μT-Kernel を展開する

[「micro:bitでμT-Kernel 3.0を動かそう」の Web ページ](https://www.t-engine4u.com/info/mbit/2.html) から `362_mbit_mtk3.zip` を入手し、展開します。以下、`362_mbit_mtk3/mtkernel_3` をトップフォルダとします。

`mtkernel_3` 下で、次のパスが見えることを確認してください。

- `kernel/`
- `lib/`
- `device/`
- `include/`
- `config/`
- `etc/`
- `app_sample/`
- `build_make/`

### 2. 本リポジトリを clone する

`mtkernel_3` の外へ clone します。`mtkernel_3` のソースツリー内で clone しないでください。

```bash
git clone https://github.com/koh523/benus.git
```

この時点で `device/`、`components/`、`etc/`、`patch/`、`build_make/`、`app_sample/` がローカル側に配置されます。

### 3. ドライバソースツリーの配置とパッチを適用する

clone 側のスクリプトに、展開済みの `mtkernel_3` を渡します。次のどちらか一方を実行してください（両方は不要です）。
`--mtk3` には `config/` と `build_make/makefile` があるディレクトリを渡します。

```bash
# Python で直接実行する
python benus/patch/apply.py --mtk3 /path/to/mtkernel_3

# または、シェルスクリプト経由で実行する
./benus/patch/apply.sh --mtk3 /path/to/mtkernel_3
```

`apply.sh` は、使える Python（`python`、`python3`、`py`）を探して `apply.py` を実行するラッパーです。どちらを使っても結果は同じです。


配置される内容は次のとおりです。PMC の `app_sample` は本リポジトリの利用例で置き換わります。

| 配置                                             | 内容                              |
| ---------------------------------------------- | ------------------------------- |
| `app_sample/`                                  | NUS エコーと 1 灯点滅の利用例（PMC サンプルを置換） |
| `device/blenus/`、`device/include/dev_blenus.h` | デバイスドライバ                        |
| `components/`                                  | SoftDevice / nRF5 SDK / BLE     |
| `build_make/blenus_overlay.mk` ほか              | overlay 用 makefile              |
| `etc/linker/microbit/tkernel_ble_wsd.ld`       | SoftDevice 用リンカスクリプト            |

続いて μT-Kernel 側に当たるパッチの対象は次のとおりです。変更の中身は `patch/` を参照してください。

- `kernel/sysdepend/cpu/core/armv7m/dispatch.S`
- `kernel/sysdepend/cpu/core/armv7m/cpu_task.h`
- `kernel/sysdepend/cpu/core/armv7m/reset_hdl.c`
- `kernel/sysdepend/cpu/core/armv7m/interrupt.c`
- `kernel/sysdepend/cpu/core/armv7m/sysdepend.h`
- `kernel/sysdepend/cpu/nrf5/vector_tbl.c`
- `kernel/sysdepend/microbit/devinit.c`
- `config/config.h`
- `config/config_device.h`
- `build_make/makefile`
- `device/include/device.h`
- `device/include/dev_def.h`
- `include/sys/sysdepend/cpu/nrf5/sysdef.h`

`blenus_overlay.mk` は本リポジトリの新規ファイルです。PMC の `makefile` / `microbit.mk` は置き換えません。

### 4. ビルドする

PMC 配布の構築手順（Eclipse または `build_make`）に従い、`mtkernel_3` 側でビルドします。コマンドラインでは、`build_make` に移動して `make` を実行します。

```bash
cd /path/to/mtkernel_3/build_make
make
```

成功すると、同じディレクトリに `mtkernel_3.elf` が生成されます。

### 5. micro:bit へ書き込む

手順 4 で生成した ELF を書き込みます。

```bash
pyocd load -t nrf52 mtkernel_3.elf
```

アプリは配置済みの `app_sample` です。`tk_opn_dev("blua")` で SoftDevice と advertising が始まります。

### 補足（動作確認）

`app_sample` の動作確認には、本リポジトリの `web/webbt_nus-simple.html` を使います。書き込み後、micro:bit は `micro:bit2_UART` として advertising します。

Chrome で `web/webbt_nus-simple.html` を開き、接続して送受信を確認してください。
