# 實時回音消除示例

基於 Mumble 的設計模式，使用 PortAudio 和 SpeexDSP 實現的實時回音消除系統。

## 功能特點

### 🎯 基於 Mumble 的最佳實踐
- **48kHz 目標採樣率**: 與 Mumble 相同的音頻質量
- **10ms 幀大小**: 低延遲處理
- **100ms 濾波器長度**: 平衡收斂速度和回音消除效果
- **智能重採樣**: 自動處理不同採樣率的音頻設備

### 🔧 音頻處理功能
- **實時回音消除**: 使用 Speex 回音消除算法
- **噪音抑制**: 集成 Speex 預處理器
- **自動增益控制 (AGC)**: 動態音量調整
- **語音活動檢測 (VAD)**: 智能語音檢測（可選）
- **多聲道支持**: 自動混合到單聲道

### 🎛️ 音頻同步機制
- **分離的麥克風和揚聲器流**: 獨立的音頻捕獲
- **Loopback 模式**: 直接捕獲揚聲器輸出（可選）
- **緩衝區管理**: 防止內存溢出和延遲積累
- **線程安全**: 多線程音頻處理

## 文件說明

### 主要文件
- **`testecho_realtime.cpp`**: 完整功能的實時回音消除示例（基於 Mumble 技術）
- **`testecho_basic.cpp`**: 基本版本，不依賴 Loopback 模式（推薦用於測試）
- **`testecho_simple.cpp`**: 簡化版本，用於基本功能測試
- **`testecho_mono.cpp`**: 離線文件處理示例

### 版本對比

| 版本 | Loopback 支持 | 重採樣 | 多聲道 | 推薦用途 |
|------|---------------|--------|--------|----------|
| `testecho_realtime.cpp` | 嘗試支持 | 完整 | 基本 | 完整功能測試 |
| `testecho_basic.cpp` | 無 | 無 | 無 | **推薦用於測試** |
| `testecho_simple.cpp` | 無 | 無 | 無 | 基本功能測試 |
| `testecho_mono.cpp` | 無 | 無 | 無 | 離線處理 |

### 基於 Mumble 的技術實現

#### 1. **音頻參數配置**
```cpp
// 基於 Mumble 的配置
static const int TARGET_SAMPLE_RATE = 48000;  // Mumble 使用 48kHz
static const int FRAME_SIZE_MS = 10;          // 10ms 幀
static const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;
static const int FILTER_LENGTH_MS = 100;      // 100ms 濾波器長度
static const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;
```

#### 2. **Resynchronizer 設計模式**
- 使用 `std::deque` 實現音頻緩衝區
- 分離的麥克風和揚聲器緩衝區
- 基於 Mumble 的 `AudioChunk` 概念

#### 3. **Speex 配置**
```cpp
// 基於 Mumble 的 Speex 設置
speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState);
speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC, &agc);
speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_VAD, &vad);
```

#### 4. **重採樣策略**
- 使用 `speex_resampler_process_interleaved_int`
- 支持任意採樣率轉換
- 基於 Mumble 的重採樣邏輯

#### 5. **聲道混合**
- 實現 Mumble 的 `inMixerFloat` 邏輯
- 自動多聲道到單聲道轉換
- 支持任意聲道數

## 技術架構

### 音頻流程
```
麥克風輸入 → 重採樣 → 混合到單聲道 → 回音消除 → 預處理 → 輸出
     ↓
揚聲器監聽 → 重採樣 → 混合到單聲道 → 回音參考
```

### 核心組件
1. **PortAudio 流管理**
   - 麥克風輸入流
   - 揚聲器監聽流 (Loopback) - 完整版本
   - 音頻輸出流

2. **SpeexDSP 處理**
   - `SpeexEchoState`: 回音消除
   - `SpeexPreprocessState`: 噪音抑制和 AGC
   - `SpeexResamplerState`: 音頻重採樣

3. **同步機制**
   - 多線程音頻處理
   - 條件變量同步
   - 緩衝區大小控制

## 編譯和運行

### 依賴項
- PortAudio
- SpeexDSP
- C++17 或更高版本

### 編譯
```bash
# 使用 vcpkg 安裝依賴
vcpkg install portaudio speexdsp

# 編譯項目
cmake --build build
```

### 運行
```bash
# 基本版本（推薦用於測試）
./testecho_basic

# 完整版本（需要 Loopback 支持）
./testecho_realtime

# 簡化版本
./testecho_simple
```

## 配置選項

### 回音消除模式
```cpp
enum EchoCancelMode {
    ECHO_DISABLED = 0,      // 禁用回音消除
    ECHO_MIXED = 1,         // 混合模式（推薦）
    ECHO_MULTICHANNEL = 2   // 多聲道模式
};
```

### 音頻參數
```cpp
static const int TARGET_SAMPLE_RATE = 48000;  // 目標採樣率
static const int FRAME_SIZE_MS = 10;          // 幀大小（毫秒）
static const int FILTER_LENGTH_MS = 100;      // 濾波器長度（毫秒）
static const int MAX_BUFFER_SIZE = 50;        // 最大緩衝區大小
```

## 故障排除

### 常見問題

1. **PortAudio 編譯錯誤**
   ```
   'paLoopback': undeclared identifier
   'Pa_OpenStream': function does not take 7 arguments
   ```
   - **原因**: PortAudio 版本不支持 Loopback 模式
   - **解決方案**: 使用 `testecho_basic.cpp` 或 `testecho_simple.cpp`

2. **VAD 警告**
   ```
   warning: The VAD has been replaced by a hack pending a complete rewrite
   ```
   - **解決方案**: 已在代碼中禁用 VAD 以避免此警告
   - **影響**: 不影響回音消除功能

3. **Loopback 模式失敗**
   ```
   Failed to open speaker stream: Invalid number of channels
   ```
   - **原因**: 某些平台不支持 Loopback 模式
   - **解決方案**: 
     - 使用 `testecho_basic.cpp`（推薦）
     - 程序會自動嘗試單聲道
     - 如果完全失敗，會禁用回音消除但繼續運行

4. **PortAudio 初始化失敗**
   - 檢查音頻設備是否可用
   - 確保沒有其他應用佔用音頻設備

5. **回音消除效果不佳**
   - 調整濾波器長度
   - 檢查麥克風和揚聲器音量
   - 確保環境噪音較低

### 平台兼容性

| 平台 | Loopback 支持 | 推薦版本 |
|------|---------------|----------|
| Windows | 部分支持 | `testecho_basic.cpp` |
| macOS | 不支持 | `testecho_basic.cpp` |
| Linux | 依賴音頻系統 | `testecho_basic.cpp` |

### 調試信息
程序運行時會顯示：
- 設備信息（採樣率、聲道數）
- 回音消除狀態
- 處理幀數統計
- 丟棄幀數統計

## 性能優化

### 緩衝區管理
- 使用 `std::deque` 實現高效的 FIFO 緩衝區
- 自動丟棄過期的音頻幀
- 防止內存泄漏和延遲積累

### 重採樣優化
- 只在需要時創建重採樣器
- 使用 Speex 的高質量重採樣算法
- 支持任意採樣率轉換

### 線程優化
- 分離的音頻捕獲和處理線程
- 最小化鎖競爭
- 使用條件變量避免忙等待

## 與 Mumble 的對比

| 特性 | Mumble | 完整版本 | 基本版本 | 簡化版本 |
|------|--------|----------|----------|----------|
| 採樣率 | 48kHz | 48kHz | 48kHz | 48kHz |
| 幀大小 | 10ms | 10ms | 10ms | 10ms |
| 濾波器長度 | 100ms | 100ms | 100ms | 100ms |
| 重採樣 | Speex | Speex | 無 | 無 |
| 回音消除 | Speex | Speex | Speex | Speex |
| 預處理 | Speex | Speex | Speex | Speex |
| 同步機制 | Resynchronizer | 簡化版本 | 基本 | 基本 |
| 多聲道支持 | 完整 | 基本 | 無 | 無 |
| Loopback | 平台特定 | 嘗試支持 | 無 | 無 |

## 基於 Mumble 的技術細節

### 1. **音頻處理流程**
```cpp
// 基於 Mumble 的 AudioInput::encodeAudioFrame 邏輯
if (sesEcho && chunk.speaker) {
    speex_echo_cancellation(echoState, chunk.mic, chunk.speaker, psClean);
    psSource = psClean;
} else {
    psSource = chunk.mic;
}
```

### 2. **重採樣實現**
```cpp
// 基於 Mumble 的重採樣邏輯
speex_resampler_process_interleaved_int(resampler, 
    input.data(), &inLen, 
    output.data(), &outLen);
```

### 3. **聲道混合**
```cpp
// 基於 Mumble 的 inMixerFloat 實現
for (size_t i = 0; i < input.size(); i += channels) {
    int32_t sum = 0;
    for (int ch = 0; ch < channels; ch++) {
        sum += input[i + ch];
    }
    output.push_back(static_cast<int16_t>(sum / channels));
}
```

### 4. **緩衝區同步**
```cpp
// 基於 Mumble 的 Resynchronizer 設計
std::deque<std::vector<int16_t>> micBuffer;
std::deque<std::vector<int16_t>> speakerBuffer;
std::condition_variable micCondition;
```

## 擴展功能

### 可添加的功能
1. **音頻可視化**: 實時頻譜顯示
2. **參數調整**: 運行時調整回音消除參數
3. **錄音功能**: 保存處理後的音頻
4. **多設備支持**: 選擇特定的音頻設備
5. **網絡傳輸**: 添加 VoIP 功能

### 代碼結構
```
RealTimeEchoCancellation/
├── 初始化 (initialize)
├── 設備檢測 (getDeviceInfo)
├── Speex 設置 (initializeSpeex)
├── 音頻流設置 (setupAudioStreams)
├── 音頻處理 (processAudio)
├── 重採樣 (resampleAudio)
├── 聲道混合 (mixToMono)
└── 清理 (cleanup)
```

## 授權

此示例基於 Mumble 的開源設計，遵循相應的開源授權條款。

## 貢獻

歡迎提交問題報告和改進建議！ 