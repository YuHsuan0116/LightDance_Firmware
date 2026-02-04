# 呼吸燈生成程式

用於生成呼吸燈效果的 control.dat 和 frame.dat 文件

## 功能

- 支持多種顏色選擇：g(綠), r(紅), b(藍), rg(紅+綠), gb(綠+藍), rb(紅+藍), w(白)
- 支持淡入淡出效果(fade)開關
- 可自定義幀間時間間隔、燈條與光纖數

## 文件格式

### control.dat 結構：
- 版本號 (2 bytes)
- OF數量 (1 byte)
- 燈條數量 (1 byte)
- 每條燈條的LED數量 (n bytes)
- 總幀數 (4 bytes)
- 每幀的時間戳 (每幀 4 bytes)

### frame.dat 結構：
- 版本號 (2 bytes)
- 每幀數據：
  - 開始時間 (4 bytes)
  - fade值 (1 byte)
  - OF通道顏色數據 (每通道 3 bytes: G, R, B)
  - LED顏色數據 (每個LED 3 bytes: G, R, B)
  - checksum (4 bytes)

## 快速開始

生成 control.dat 和 frame.dat：

```
python breath.py
```

讀取檔案：

```
python read_dat.py
```