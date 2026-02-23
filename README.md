# i2c-input-device
## 概要
- I2C スレーブとして、入力状態を 2バイト固定で返す
- Core: ATmega328P, 3.3V, internal 8MHz
- ボタン 5個 + ロータリーエンコーダ（A/B, スイッチ無し）
- GitHubの練習のためのリポジトリ
- 未完成

## 出力
- I2C 応答フォーマット（Read 2 bytes）
  - Byte0: enc_delta (int8_t)
      - 前回 Read 以降の差分
      - -128..127 に飽和（超えたら status=001 を立てる）
      - Read したタイミングで g_encAcc を 0 にリセット（差分式）

  - Byte1: 上位3bit=status, 下位5bit=buttons
    - bit0: center
    - bit1: up
    - bit2: right
    - bit3: down
    - bit4: left
    - status:
      - 000 = 正常
      - 001 = overflow（飽和/内部飽和などの異常を検出）

## イメージ
動作テスト

<img width="400" height="300" alt="動作テスト" src="image/%E3%83%86%E3%82%B9%E3%83%88%E3%83%87%E3%83%90%E3%82%A4%E3%82%B9.jpg" />

外観

<img width="400" height="300" alt="外観" src="image/%E5%A4%96%E8%A6%B3.jpg" />

基板

<img width="400" height="300" alt="基板表" src="image/%E5%9F%BA%E6%9D%BF%E8%A1%A8.jpg" />
<img width="400" height="300" alt="基板裏" src="image/%E5%9F%BA%E6%9D%BF%E8%A3%8F.jpg" />

回路図

<img width="400" height="300" alt="回路図" src="image/%E5%9B%9E%E8%B7%AF%E5%9B%B3.png" />

3D

<img width="400" height="300" alt="3D" src="image/3D.png" />
