# spaceWombat

ESP32-C3 で [3Dconnexion SpaceMouse Pro Wireless](https://3dconnexion.com/) 互換の Bluetooth HID をエミュレートする PlatformIO プロジェクトです。アナログジョイスティック入力、またはシリアルコマンドで 6 軸とボタンを送れます。

## ハードウェア

| 項目 | 内容 |
|------|------|
| ボード | ESP32-C3 SuperMini（`esp32-c3-devkitm-1`） |
| ジョイスティック X | `A4` → 軸 X |
| ジョイスティック Y | `A3` → 軸 Z |
| 基板 LED | GPIO 8（アクティブ Low） |

## ビルド・書き込み

[PlatformIO](https://platformio.org/) が必要です。

```bash
pio run -t upload
pio device monitor
```

シリアルモニタは 115200 bps です。

## 動作概要

- BLE で HID デバイス名 `SpaceMouse Pro Wireless`（VID `0x256F` / PID `0xC638`）として公開
- 既定入力はジョイスティック（パン: X / Z）
- 接続後、軸・ボタンレポートをホストへ通知

## シリアルコマンド

モニタから以下を送れます（`?` で一覧）。

| コマンド | 説明 |
|----------|------|
| `A x y z rx ry rz` | 軸を手動設定（範囲 -350..350） |
| `B <hex>` | ボタン 32bit |
| `L <0-100>` | 電池残量 |
| `Z` | 軸・ボタンをゼロにし、ジョイスティック入力へ戻す |
| `D` | デモ開始 / 停止 |
| `J` | ジョイスティック中心を再キャリブレーション |
| `V` | ジョイスティック生値ログの切替 |
| `P` | 接続・軸・ボタンなどの状態表示 |

## 構成

```
├── platformio.ini
├── include/
│   └── SpaceMouseReport.h   # HID レポート定義
└── src/
    └── main.cpp             # NimBLE HID / ジョイスティック / シリアル
```

依存ライブラリ: [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 1.4.3

## 注意

ホスト側の 3Dconnexion ソフトウェアやアプリが正規 SpaceMouse として認識するかは環境依存です。学習・実験用途を想定しています。
