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
動作確認

<img width="400" height="300" alt="動作テスト" src="images/operation_confirmation.jpg" />

外観

<img width="400" height="300" alt="外観" src="images/input_device_exterior.jpg" />

基板

<img width="400" height="300" alt="基板表" src="images/input_device_circuit_board_front.jpg" />
<img width="400" height="300" alt="基板裏" src="images/input_device_circuit_board_back.jpg" />

回路図

<img width="400" height="300" alt="回路図" src="images/input_device_circuit_diagram.png" />

3Dモデル

<img width="400" height="300" alt="3D" src="images/input_device_3D_model.png" />

テスト用デバイス

<img width="400" height="300" alt="3D" src="images/test_device_exterior.jpg" />
