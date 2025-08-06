using ConsoleApp1;
using NAudio.Wave;
using NAudio.CoreAudioApi;
using SpeexDSPSharp.Core;
using SpeexDSPSharp.Core.SafeHandlers;
using System.Collections.Concurrent;

// 基於 Mumble 的音頻參數配置
const int TARGET_SAMPLE_RATE = 48000;  // Mumble 使用 48kHz
const int FRAME_SIZE_MS = 10;          // 10ms 幀
const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;  // 480 samples
const int FILTER_LENGTH_MS = 100;      // 100ms 濾波器長度
const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;  // 4800 samples
const int CHANNELS = 1;                // 單聲道
const int MAX_BUFFER_SIZE = 50;        // 最大緩衝區大小

var format = new WaveFormat(TARGET_SAMPLE_RATE, 16, CHANNELS);
var bufferSize = FRAME_SIZE * 2; // 16-bit mono = 2 bytes per sample

Console.WriteLine("=========================================");
Console.WriteLine("Real-Time Echo Cancellation Demo (C#)");
Console.WriteLine("Based on Mumble's Echo Cancellation Design");
Console.WriteLine("=========================================");
Console.WriteLine($"Target Sample Rate: {TARGET_SAMPLE_RATE}Hz");
Console.WriteLine($"Frame Size: {FRAME_SIZE} samples ({FRAME_SIZE_MS}ms)");
Console.WriteLine($"Filter Length: {FILTER_LENGTH} samples ({FILTER_LENGTH_MS}ms)");
Console.WriteLine($"Channels: {CHANNELS}");

// 基於 Mumble 設計的回音消除器
var echoCanceller = new MumbleEchoCancellation(FRAME_SIZE_MS, FILTER_LENGTH_MS, format);

// 音頻緩衝區 - 基於 Mumble 的 Resynchronizer 設計
var micBuffer = new ConcurrentQueue<byte[]>();
var speakerBuffer = new ConcurrentQueue<byte[]>();
var outputBuffer = new ConcurrentQueue<byte[]>();

// 同步機制
var processingCancellationToken = new CancellationTokenSource();
var micProcessed = new ManualResetEvent(false);
var speakerProcessed = new ManualResetEvent(false);

// 統計信息
var processedFrames = 0L;
var droppedFrames = 0L;

// 系統音頻捕獲（揚聲器輸出監聽）
var systemAudioCapture = new WasapiLoopbackCapture();

// 創建緩衝區提供者來處理系統音頻
var systemAudioBuffer = new BufferedWaveProvider(systemAudioCapture.WaveFormat)
{
    BufferLength = bufferSize * 100,
    DiscardOnBufferOverflow = true
};

// 創建重採樣器將系統音頻轉換為目標格式
var systemAudioResampler = new MediaFoundationResampler(systemAudioBuffer, format);

// 麥克風錄音設備
var recorder = new WaveInEvent()
{
    WaveFormat = format,
    BufferMilliseconds = FRAME_SIZE_MS
};

// 音頻輸出
var output = new WaveOutEvent();
var playbackBuffer = new BufferedWaveProvider(format)
{
    BufferLength = bufferSize * 100,
    DiscardOnBufferOverflow = true
};
output.Init(playbackBuffer);

// 輸出 WAV 文件
var outputFile = $"aec_clean_{TARGET_SAMPLE_RATE}_{DateTime.Now:HHmmss}.wav";
var writer = new WaveFileWriter(outputFile, format);

// 基於 Mumble 的音頻處理線程
var processingTask = Task.Run(async () =>
{
    Console.WriteLine("Audio processing started");
    Console.WriteLine("Echo cancellation mode: Enabled");

    while (!processingCancellationToken.Token.IsCancellationRequested)
    {
        try
        {
            // 等待麥克風數據 - 基於 Mumble 的 Resynchronizer 邏輯
            if (micBuffer.TryDequeue(out var micData))
            {
                // 獲取揚聲器數據
                byte[] speakerData;
                if (!speakerBuffer.TryDequeue(out speakerData))
                {
                    // 沒有揚聲器數據，使用靜音
                    speakerData = new byte[bufferSize];
                }

                // 確保緩衝區大小正確
                if (micData.Length != bufferSize)
                {
                    Array.Resize(ref micData, bufferSize);
                }
                if (speakerData.Length != bufferSize)
                {
                    Array.Resize(ref speakerData, bufferSize);
                }

                var outputData = new byte[bufferSize];

                // 執行回音消除 - 基於 Mumble 的 speex_echo_cancellation
                echoCanceller.Cancel(micData, speakerData, outputData);

                // 添加到輸出緩衝區
                outputBuffer.Enqueue(outputData);

                // 限制輸出緩衝區大小
                while (outputBuffer.Count > MAX_BUFFER_SIZE)
                {
                    outputBuffer.TryDequeue(out _);
                }

                // 寫入 WAV 文件
                writer.Write(outputData, 0, outputData.Length);

                Interlocked.Increment(ref processedFrames);
            }
            else
            {
                // 沒有麥克風數據，等待一下
                await Task.Delay(1, processingCancellationToken.Token);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error in audio processing: {ex.Message}");
            await Task.Delay(10, processingCancellationToken.Token);
        }
    }
}, processingCancellationToken.Token);

// 系統音頻處理 - 基於 Mumble 的 addEcho 設計
systemAudioCapture.DataAvailable += (s, e) =>
{
    try
    {
        // 將捕獲的系統音頻添加到緩衝區
        systemAudioBuffer.AddSamples(e.Buffer, 0, e.BytesRecorded);

        // 重採樣系統音頻到目標格式
        var resampledBuffer = new byte[bufferSize];
        int resampledBytes = systemAudioResampler.Read(resampledBuffer, 0, bufferSize);

        if (resampledBytes > 0)
        {
            // 確保緩衝區大小正確
            if (resampledBytes != bufferSize)
            {
                Array.Resize(ref resampledBuffer, bufferSize);
            }

            // 添加到揚聲器緩衝區
            speakerBuffer.Enqueue(resampledBuffer);

            // 限制緩衝區大小
            while (speakerBuffer.Count > MAX_BUFFER_SIZE)
            {
                speakerBuffer.TryDequeue(out _);
            }

            // 提供參考信號給回音消除器
            echoCanceller.EchoPlayBack(resampledBuffer);
        }
    }
    catch (Exception ex)
    {
        Console.WriteLine($"Error processing system audio: {ex.Message}");
    }
};

// 麥克風錄音處理 - 基於 Mumble 的 AudioInput 設計
recorder.DataAvailable += (s, e) =>
{
    try
    {
        var micData = new byte[bufferSize];
        Buffer.BlockCopy(e.Buffer, 0, micData, 0, Math.Min(e.BytesRecorded, bufferSize));

        // 確保緩衝區大小正確
        if (e.BytesRecorded != bufferSize)
        {
            Array.Resize(ref micData, bufferSize);
        }

        // 添加到麥克風緩衝區
        micBuffer.Enqueue(micData);

        // 限制緩衝區大小
        while (micBuffer.Count > MAX_BUFFER_SIZE)
        {
            micBuffer.TryDequeue(out _);
            Interlocked.Increment(ref droppedFrames);
        }
    }
    catch (Exception ex)
    {
        Console.WriteLine($"Error processing microphone audio: {ex.Message}");
    }
};

// 音頻輸出處理
var outputTask = Task.Run(async () =>
{
    while (!processingCancellationToken.Token.IsCancellationRequested)
    {
        try
        {
            if (outputBuffer.TryDequeue(out var outputData))
            {
                playbackBuffer.AddSamples(outputData, 0, outputData.Length);
            }
            else
            {
                // 沒有輸出數據，等待一下
                await Task.Delay(1, processingCancellationToken.Token);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error in output processing: {ex.Message}");
            await Task.Delay(10, processingCancellationToken.Token);
        }
    }
}, processingCancellationToken.Token);

// 統計報告任務
var statsTask = Task.Run(async () =>
{
    while (!processingCancellationToken.Token.IsCancellationRequested)
    {
        await Task.Delay(5000, processingCancellationToken.Token); // 每5秒報告一次
        
        var currentProcessed = Interlocked.Read(ref processedFrames);
        var currentDropped = Interlocked.Read(ref droppedFrames);
        
        Console.WriteLine($"Status - Processed: {currentProcessed}, Dropped: {currentDropped}, " +
                         $"Mic Buffer: {micBuffer.Count}, Speaker Buffer: {speakerBuffer.Count}, " +
                         $"Output Buffer: {outputBuffer.Count}");
    }
}, processingCancellationToken.Token);

// 啟動所有組件
Console.WriteLine("Starting real-time echo cancellation...");

systemAudioCapture.StartRecording();
recorder.StartRecording();
output.Play();

Console.WriteLine("Real-time echo cancellation started successfully!");
Console.WriteLine("Press Enter to stop...");

Console.ReadLine();

// 清理
Console.WriteLine("Stopping real-time echo cancellation...");

processingCancellationToken.Cancel();

systemAudioCapture.StopRecording();
recorder.StopRecording();
output.Stop();

// 等待任務完成
Task.WaitAll(new[] { processingTask, outputTask, statsTask }, 5000);

// 清理資源
systemAudioResampler?.Dispose();
writer.Dispose();
echoCanceller.Dispose();

var finalProcessed = Interlocked.Read(ref processedFrames);
var finalDropped = Interlocked.Read(ref droppedFrames);

Console.WriteLine("Real-time echo cancellation stopped.");
Console.WriteLine($"Final Stats - Processed frames: {finalProcessed}");
Console.WriteLine($"Final Stats - Dropped frames: {finalDropped}");
Console.WriteLine($"Output file: {outputFile}");

namespace ConsoleApp1
{
    /// <summary>
    /// 基於 Mumble 設計的回音消除器
    /// 使用 Mumble 的音頻參數和處理邏輯
    /// </summary>
    public class MumbleEchoCancellation : IDisposable
    {
        private readonly CustomSpeexDSPEchoCanceler _canceller;
        private readonly CustomSpeexDSPPreprocessor _preprocessor;
        private readonly int _frameSize;
        private readonly int _sampleRate;
        private readonly WaveFormat _waveFormat;

        public WaveFormat WaveFormat => _waveFormat;

        public unsafe MumbleEchoCancellation(int frameSizeMS, int filterLengthMS, WaveFormat format)
        {
            _waveFormat = format;
            _sampleRate = format.SampleRate;

            // 計算幀大小和濾波器長度（基於 Mumble 的計算方式）
            _frameSize = (frameSizeMS * _sampleRate) / 1000;
            var filterLength = (filterLengthMS * _sampleRate) / 1000;

            Console.WriteLine($"Initializing Mumble Echo Cancellation:");
            Console.WriteLine($"  Frame Size: {_frameSize} samples");
            Console.WriteLine($"  Filter Length: {filterLength} samples");
            Console.WriteLine($"  Sample Rate: {_sampleRate}Hz");

            // 初始化回音消除器 - 基於 Mumble 的 speex_echo_state_init
            _canceller = new CustomSpeexDSPEchoCanceler(_frameSize, filterLength);

            // 設置採樣率 - 基於 Mumble 的 speex_echo_ctl
            _canceller.Ctl(EchoCancellationCtl.SPEEX_ECHO_SET_SAMPLING_RATE, ref _sampleRate);

            // 初始化預處理器 - 基於 Mumble 的 speex_preprocess_state_init
            _preprocessor = new CustomSpeexDSPPreprocessor(_frameSize, _sampleRate);

            // 關聯回音消除和預處理 - 基於 Mumble 的設計
            var echoStatePtr = _canceller.Handler.DangerousGetHandle();
            if (NativeSpeexDSP.speex_preprocess_ctl(_preprocessor.Handler, 
                PreprocessorCtl.SPEEX_PREPROCESS_SET_ECHO_STATE.GetHashCode(), 
                echoStatePtr.ToPointer()) == 0)
            {
                Console.WriteLine("Preprocessor linked with echo canceller successfully.");
            }

            // 配置預處理器 - 基於 Mumble 的設置
            ConfigurePreprocessor();
        }

        private void ConfigurePreprocessor()
        {
            try
            {
                // 基於 Mumble 的預處理器配置
                int denoise = 1;        // 啟用降噪
                int agc = 1;            // 啟用自動增益控制
                int vad = 0;            // 禁用 VAD 以避免警告
                int agcLevel = 8000;    // AGC 目標電平
                int agcMaxGain = 20000; // AGC 最大增益
                int agcIncrement = 12;  // AGC 增量
                int agcDecrement = -40; // AGC 減量

                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_DENOISE, ref denoise);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC, ref agc);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_VAD, ref vad);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_TARGET, ref agcLevel);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, ref agcMaxGain);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_INCREMENT, ref agcIncrement);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_DECREMENT, ref agcDecrement);

                Console.WriteLine("Preprocessor configured with Mumble settings:");
                Console.WriteLine($"  Denoise: {denoise}");
                Console.WriteLine($"  AGC: {agc}");
                Console.WriteLine($"  VAD: {vad}");
                Console.WriteLine($"  AGC Target: {agcLevel}");
                Console.WriteLine($"  AGC Max Gain: {agcMaxGain}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Could not configure all preprocessor settings: {ex.Message}");
            }
        }

        /// <summary>
        /// 執行回音消除 - 基於 Mumble 的 speex_echo_cancellation 和 speex_preprocess_run
        /// </summary>
        /// <param name="referenceBuffer">揚聲器參考信號</param>
        /// <param name="capturedBuffer">麥克風捕獲信號</param>
        /// <param name="outputBuffer">回音消除後的輸出</param>
        public void Cancel(byte[] referenceBuffer, byte[] capturedBuffer, byte[] outputBuffer)
        {
            // 確保所有緩衝區大小正確
            var frameBytes = _frameSize * 2; // 16-bit = 2 bytes per sample

            if (referenceBuffer.Length != frameBytes ||
                capturedBuffer.Length != frameBytes ||
                outputBuffer.Length != frameBytes)
            {
                Array.Resize(ref referenceBuffer, frameBytes);
                Array.Resize(ref capturedBuffer, frameBytes);
                Array.Resize(ref outputBuffer, frameBytes);
            }

            // 執行回音消除 - 基於 Mumble 的 speex_echo_cancellation
            _canceller.EchoCancel(referenceBuffer, capturedBuffer, outputBuffer);

            // 應用預處理 - 基於 Mumble 的 speex_preprocess_run
            _preprocessor.Run(outputBuffer);
        }

        /// <summary>
        /// 提供參考信號給回音消除器 - 基於 Mumble 的 addEcho 設計
        /// </summary>
        /// <param name="echoPlayback">系統音頻緩衝區</param>
        public void EchoPlayBack(byte[] echoPlayback)
        {
            _canceller.EchoPlayback(echoPlayback);
        }

        /// <summary>
        /// 清理資源
        /// </summary>
        public void Dispose()
        {
            _canceller?.Dispose();
            _preprocessor?.Dispose();
        }
    }

    public class CustomSpeexDSPPreprocessor(int frame_size, int filter_length) : SpeexDSPPreprocessor(frame_size, filter_length)
    {
        public SpeexDSPPreprocessStateSafeHandler Handler => base._handler;
    }

    public class CustomSpeexDSPEchoCanceler(int frame_size, int filter_length) : SpeexDSPEchoCanceler(frame_size, filter_length)
    {
        public SpeexDSPEchoStateSafeHandler Handler => base._handler;
    }
}