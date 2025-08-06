#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <deque>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <portaudio.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <speex/speex_resampler.h>

// 基於 Mumble 的設計模式
class RealTimeEchoCancellation {
private:
    // 音頻參數 - 基於 Mumble 的配置
    static const int TARGET_SAMPLE_RATE = 48000;  // Mumble 使用 48kHz
    static const int FRAME_SIZE_MS = 10;          // 10ms 幀
    static const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;  // 480 samples
    static const int FILTER_LENGTH_MS = 100;      // 100ms 濾波器長度
    static const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;
    static const int CHANNELS = 1;                // 單聲道
    static const int MAX_BUFFER_SIZE = 50;        // 最大緩衝區大小

    // PortAudio 流
    PaStream* micStream;
    PaStream* speakerStream;
    PaStream* outputStream;

    // Speex 組件
    SpeexEchoState* echoState;
    SpeexPreprocessState* preprocessState;
    SpeexResamplerState* micResampler;
    SpeexResamplerState* speakerResampler;

    // 音頻緩衝區 - 基於 Mumble 的 Resynchronizer 設計
    std::deque<std::vector<int16_t>> micBuffer;
    std::deque<std::vector<int16_t>> speakerBuffer;
    std::deque<std::vector<int16_t>> outputBuffer;

    // 同步機制 - 基於 Mumble 的 Resynchronizer
    std::mutex micMutex;
    std::mutex speakerMutex;
    std::mutex outputMutex;
    std::condition_variable micCondition;
    std::condition_variable outputCondition;
    std::atomic<bool> running;

    // 處理線程
    std::thread processingThread;

    // 音頻設備信息
    int micSampleRate;
    int speakerSampleRate;
    int micChannels;
    int speakerChannels;

    // 回音消除選項 - 基於 Mumble 的設計
    enum EchoCancelMode {
        ECHO_DISABLED = 0,
        ECHO_MIXED = 1,
        ECHO_MULTICHANNEL = 2
    };
    EchoCancelMode echoMode;

    // 統計信息
    std::atomic<int64_t> processedFrames;
    std::atomic<int64_t> droppedFrames;

public:
    RealTimeEchoCancellation() :
        micStream(nullptr),
        speakerStream(nullptr),
        outputStream(nullptr),
        echoState(nullptr),
        preprocessState(nullptr),
        micResampler(nullptr),
        speakerResampler(nullptr),
        running(false),
        micSampleRate(0),
        speakerSampleRate(0),
        micChannels(0),
        speakerChannels(0),
        echoMode(ECHO_MIXED),
        processedFrames(0),
        droppedFrames(0) {
    }

    ~RealTimeEchoCancellation() {
        cleanup();
    }

    bool initialize() {
        std::cout << "Initializing Real-Time Echo Cancellation..." << std::endl;

        // 初始化 PortAudio
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // 獲取設備信息
        if (!getDeviceInfo()) {
            return false;
        }

        // 初始化 Speex 回音消除
        if (!initializeSpeex()) {
            return false;
        }

        // 設置音頻流
        if (!setupAudioStreams()) {
            return false;
        }

        std::cout << "Initialization completed successfully!" << std::endl;
        std::cout << "Mic: " << micSampleRate << "Hz, " << micChannels << " channels" << std::endl;
        std::cout << "Speaker: " << speakerSampleRate << "Hz, " << speakerChannels << " channels" << std::endl;
        std::cout << "Target: " << TARGET_SAMPLE_RATE << "Hz, " << CHANNELS << " channels" << std::endl;

        return true;
    }

private:
    bool getDeviceInfo() {
        // 獲取默認輸入設備
        PaDeviceIndex micDevice = Pa_GetDefaultInputDevice();
        if (micDevice == paNoDevice) {
            std::cerr << "No default input device found" << std::endl;
            return false;
        }

        const PaDeviceInfo* micInfo = Pa_GetDeviceInfo(micDevice);
        if (!micInfo) {
            std::cerr << "Failed to get input device info" << std::endl;
            return false;
        }

        micSampleRate = static_cast<int>(micInfo->defaultSampleRate);
        micChannels = micInfo->maxInputChannels;

        // 獲取默認輸出設備
        PaDeviceIndex speakerDevice = Pa_GetDefaultOutputDevice();
        if (speakerDevice == paNoDevice) {
            std::cerr << "No default output device found" << std::endl;
            return false;
        }

        const PaDeviceInfo* speakerInfo = Pa_GetDeviceInfo(speakerDevice);
        if (!speakerInfo) {
            std::cerr << "Failed to get output device info" << std::endl;
            return false;
        }

        speakerSampleRate = static_cast<int>(speakerInfo->defaultSampleRate);
        speakerChannels = speakerInfo->maxOutputChannels;

        return true;
    }

    bool initializeSpeex() {
        // 初始化回音消除狀態 - 基於 Mumble 的配置
        echoState = speex_echo_state_init(FRAME_SIZE, FILTER_LENGTH);
        if (!echoState) {
            std::cerr << "Failed to initialize Speex echo state" << std::endl;
            return false;
        }

        // 設置採樣率
        int sampleRate = TARGET_SAMPLE_RATE;
        speex_echo_ctl(echoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);

        // 初始化預處理器
        preprocessState = speex_preprocess_state_init(FRAME_SIZE, TARGET_SAMPLE_RATE);
        if (!preprocessState) {
            std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
            return false;
        }

        // 關聯回音消除和預處理 - 基於 Mumble 的設計
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState);

        // 配置預處理器 - 基於 Mumble 的設置
        int denoise = 1;
        int agc = 1;
        int vad = 0;  // 禁用 VAD 以避免警告
        int agcLevel = 8000;
        int agcMaxGain = 20000;
        int agcIncrement = 12;
        int agcDecrement = -40;
        
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC, &agc);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_VAD, &vad);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_TARGET, &agcLevel);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &agcMaxGain);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &agcIncrement);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &agcDecrement);

        // 初始化重採樣器 - 基於 Mumble 的重採樣策略
        int err = 0;
        if (micSampleRate != TARGET_SAMPLE_RATE) {
            micResampler = speex_resampler_init(1, micSampleRate, TARGET_SAMPLE_RATE, 3, &err);
            if (err != 0) {
                std::cerr << "Failed to initialize mic resampler" << std::endl;
                return false;
            }
        }

        if (speakerSampleRate != TARGET_SAMPLE_RATE) {
            speakerResampler = speex_resampler_init(1, speakerSampleRate, TARGET_SAMPLE_RATE, 3, &err);
            if (err != 0) {
                std::cerr << "Failed to initialize speaker resampler" << std::endl;
                return false;
            }
        }

        return true;
    }

    bool setupAudioStreams() {
        // 設置麥克風輸入流
        PaStreamParameters micParams;
        micParams.device = Pa_GetDefaultInputDevice();
        micParams.channelCount = micChannels;
        micParams.sampleFormat = paInt16;
        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
        micParams.hostApiSpecificStreamInfo = nullptr;

        // 設置揚聲器監聽流（嘗試使用 loopback 模式）
        PaStreamParameters speakerParams;
        speakerParams.device = Pa_GetDefaultOutputDevice();
        speakerParams.channelCount = speakerChannels;
        speakerParams.sampleFormat = paInt16;
        speakerParams.suggestedLatency = Pa_GetDeviceInfo(speakerParams.device)->defaultLowOutputLatency;
        speakerParams.hostApiSpecificStreamInfo = nullptr;

        // 設置輸出流
        PaStreamParameters outputParams;
        outputParams.device = Pa_GetDefaultOutputDevice();
        outputParams.channelCount = CHANNELS;
        outputParams.sampleFormat = paInt16;
        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = nullptr;

        // 打開麥克風流
        PaError err = Pa_OpenStream(&micStream,
            &micParams,
            nullptr,
            micSampleRate,
            FRAME_SIZE,
            paClipOff,
            micCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open mic stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // 嘗試打開揚聲器監聽流（使用 loopback）
        // 注意：Loopback 模式在某些 PortAudio 版本中可能不支持
        err = Pa_OpenStream(&speakerStream,
            &speakerParams,
            nullptr,
            speakerSampleRate,
            FRAME_SIZE,
            paClipOff,  // 移除 paLoopback 標誌
            speakerCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open speaker stream: " << Pa_GetErrorText(err) << std::endl;
            std::cerr << "Trying alternative approach..." << std::endl;
            
            // 嘗試使用單聲道
            speakerParams.channelCount = 1;
            err = Pa_OpenStream(&speakerStream,
                &speakerParams,
                nullptr,
                speakerSampleRate,
                FRAME_SIZE,
                paClipOff,  // 移除 paLoopback 標誌
                speakerCallback,
                this);
                
            if (err != paNoError) {
                std::cerr << "Failed to open speaker stream with mono: " << Pa_GetErrorText(err) << std::endl;
                std::cerr << "Echo cancellation will be disabled (no speaker monitoring)" << std::endl;
                
                // 如果 speaker 流完全失敗，禁用回音消除
                echoMode = ECHO_DISABLED;
                speakerStream = nullptr;
            } else {
                std::cout << "Speaker stream opened with mono" << std::endl;
                speakerChannels = 1;  // 更新為單聲道
            }
        } else {
            std::cout << "Speaker stream opened successfully" << std::endl;
        }

        // 打開輸出流
        err = Pa_OpenStream(&outputStream,
            nullptr,
            &outputParams,
            TARGET_SAMPLE_RATE,
            FRAME_SIZE,
            paClipOff,
            outputCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open output stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        return true;
    }

public:
    // 麥克風回調 - 基於 Mumble 的 AudioInput 設計
    static int micCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
        const int16_t* input = static_cast<const int16_t*>(inputBuffer);

        if (input) {
            std::vector<int16_t> frame(input, input + framesPerBuffer * ec->micChannels);

            std::lock_guard<std::mutex> lock(ec->micMutex);
            ec->micBuffer.push_back(frame);

            // 限制緩衝區大小
            while (ec->micBuffer.size() > MAX_BUFFER_SIZE) {
                ec->micBuffer.pop_front();
                ec->droppedFrames++;
            }

            ec->micCondition.notify_one();
        }

        return paContinue;
    }

    // 揚聲器回調 - 基於 Mumble 的 addEcho 設計
    static int speakerCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
        const int16_t* input = static_cast<const int16_t*>(inputBuffer);

        if (input) {
            std::vector<int16_t> frame(input, input + framesPerBuffer * ec->speakerChannels);

            std::lock_guard<std::mutex> lock(ec->speakerMutex);
            ec->speakerBuffer.push_back(frame);

            // 限制緩衝區大小
            while (ec->speakerBuffer.size() > MAX_BUFFER_SIZE) {
                ec->speakerBuffer.pop_front();
            }
        }

        return paContinue;
    }

    // 輸出回調
    static int outputCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
        int16_t* output = static_cast<int16_t*>(outputBuffer);

        std::unique_lock<std::mutex> lock(ec->outputMutex);

        if (!ec->outputBuffer.empty()) {
            std::vector<int16_t> frame = ec->outputBuffer.front();
            ec->outputBuffer.pop_front();
            lock.unlock();

            std::copy(frame.begin(), frame.end(), output);
        } else {
            // 輸出靜音
            std::fill(output, output + framesPerBuffer, 0);
        }

        return paContinue;
    }

    // 音頻處理 - 基於 Mumble 的 Resynchronizer 邏輯
    void processAudio() {
        std::vector<int16_t> micFrame(FRAME_SIZE);
        std::vector<int16_t> speakerFrame(FRAME_SIZE);
        std::vector<int16_t> outputFrame(FRAME_SIZE);
        std::vector<int16_t> resampledMicFrame;
        std::vector<int16_t> resampledSpeakerFrame;

        std::cout << "Audio processing started" << std::endl;
        std::cout << "Echo cancellation mode: " << (echoMode == ECHO_DISABLED ? "Disabled" : "Enabled") << std::endl;

        while (running) {
            // 等待麥克風數據
            std::unique_lock<std::mutex> micLock(micMutex);
            micCondition.wait(micLock, [this] { return !micBuffer.empty() || !running; });

            if (!running) break;

            if (!micBuffer.empty()) {
                std::vector<int16_t> rawMicFrame = micBuffer.front();
                micBuffer.pop_front();
                micLock.unlock();

                // 獲取揚聲器數據 - 基於 Mumble 的 Resynchronizer::addSpeaker
                std::vector<int16_t> rawSpeakerFrame;
                if (speakerStream && !speakerBuffer.empty()) {
                    std::lock_guard<std::mutex> speakerLock(speakerMutex);
                    rawSpeakerFrame = speakerBuffer.front();
                    speakerBuffer.pop_front();
                } else {
                    // 沒有揚聲器數據，使用靜音
                    rawSpeakerFrame.resize(FRAME_SIZE * speakerChannels, 0);
                }

                // 重採樣麥克風數據 - 基於 Mumble 的重採樣邏輯
                if (micResampler) {
                    resampledMicFrame = resampleAudio(rawMicFrame, micResampler, micChannels);
                } else {
                    resampledMicFrame = rawMicFrame;
                }

                // 重採樣揚聲器數據
                if (speakerResampler && !rawSpeakerFrame.empty()) {
                    resampledSpeakerFrame = resampleAudio(rawSpeakerFrame, speakerResampler, speakerChannels);
                } else {
                    resampledSpeakerFrame = rawSpeakerFrame;
                }

                // 混合多聲道到單聲道 - 基於 Mumble 的 inMixerFunc
                if (micChannels > 1) {
                    resampledMicFrame = mixToMono(resampledMicFrame, micChannels);
                }
                if (speakerChannels > 1) {
                    resampledSpeakerFrame = mixToMono(resampledSpeakerFrame, speakerChannels);
                }

                // 確保幀大小正確
                if (resampledMicFrame.size() != FRAME_SIZE) {
                    resampledMicFrame.resize(FRAME_SIZE, 0);
                }
                if (resampledSpeakerFrame.size() != FRAME_SIZE) {
                    resampledSpeakerFrame.resize(FRAME_SIZE, 0);
                }

                // 執行回音消除 - 基於 Mumble 的 speex_echo_cancellation 調用
                if (echoMode != ECHO_DISABLED && speakerStream) {
                    speex_echo_cancellation(echoState,
                        resampledSpeakerFrame.data(),
                        resampledMicFrame.data(),
                        outputFrame.data());
                } else {
                    // 如果回音消除被禁用或沒有揚聲器流，直接使用麥克風數據
                    std::copy(resampledMicFrame.begin(), resampledMicFrame.end(), outputFrame.begin());
                }

                // 應用預處理 - 基於 Mumble 的 speex_preprocess_run
                speex_preprocess_run(preprocessState, outputFrame.data());

                // 添加到輸出緩衝區
                std::lock_guard<std::mutex> outputLock(outputMutex);
                outputBuffer.push_back(outputFrame);

                // 限制輸出緩衝區大小
                while (outputBuffer.size() > MAX_BUFFER_SIZE) {
                    outputBuffer.pop_front();
                }

                processedFrames++;
            }
        }
    }

private:
    // 重採樣函數 - 基於 Mumble 的 speex_resampler_process_interleaved_int
    std::vector<int16_t> resampleAudio(const std::vector<int16_t>& input, SpeexResamplerState* resampler, int channels) {
        std::vector<int16_t> output;
        
        if (!resampler) return input;

        // 計算輸出大小
        spx_uint32_t inLen = static_cast<spx_uint32_t>(input.size() / channels);
        spx_uint32_t outLen = static_cast<spx_uint32_t>((inLen * TARGET_SAMPLE_RATE) / 
                                                       (channels == 1 ? micSampleRate : speakerSampleRate));

        output.resize(outLen * channels);

        speex_resampler_process_interleaved_int(resampler, 
            input.data(), &inLen, 
            output.data(), &outLen);

        output.resize(outLen * channels);
        return output;
    }

    // 聲道混合函數 - 基於 Mumble 的 inMixerFloat 宏
    std::vector<int16_t> mixToMono(const std::vector<int16_t>& input, int channels) {
        if (channels == 1) return input;

        std::vector<int16_t> output;
        output.reserve(input.size() / channels);

        for (size_t i = 0; i < input.size(); i += channels) {
            int32_t sum = 0;
            for (int ch = 0; ch < channels; ch++) {
                sum += input[i + ch];
            }
            output.push_back(static_cast<int16_t>(sum / channels));
        }

        return output;
    }

public:
    bool start() {
        if (running) return false;

        std::cout << "Starting real-time echo cancellation..." << std::endl;

        running = true;

        // 啟動處理線程
        processingThread = std::thread(&RealTimeEchoCancellation::processAudio, this);

        // 啟動麥克風流
        PaError err = Pa_StartStream(micStream);
        if (err != paNoError) {
            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            return false;
        }

        // 啟動揚聲器流（如果可用）
        if (speakerStream) {
            err = Pa_StartStream(speakerStream);
            if (err != paNoError) {
                std::cerr << "Failed to start speaker stream: " << Pa_GetErrorText(err) << std::endl;
                std::cerr << "Continuing without speaker monitoring..." << std::endl;
                echoMode = ECHO_DISABLED;
            } else {
                std::cout << "Speaker monitoring started successfully" << std::endl;
            }
        } else {
            std::cout << "No speaker stream available - echo cancellation disabled" << std::endl;
        }

        // 啟動輸出流
        err = Pa_StartStream(outputStream);
        if (err != paNoError) {
            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            Pa_StopStream(micStream);
            if (speakerStream) Pa_StopStream(speakerStream);
            return false;
        }

        std::cout << "Real-time echo cancellation started successfully!" << std::endl;
        std::cout << "Status: " << (echoMode == ECHO_DISABLED ? "Echo cancellation disabled" : "Echo cancellation enabled") << std::endl;
        std::cout << "Press Enter to stop..." << std::endl;

        return true;
    }

    void stop() {
        if (!running) return;

        std::cout << "Stopping real-time echo cancellation..." << std::endl;

        running = false;

        // 停止音頻流
        if (micStream) Pa_StopStream(micStream);
        if (speakerStream) Pa_StopStream(speakerStream);
        if (outputStream) Pa_StopStream(outputStream);

        // 喚醒處理線程
        micCondition.notify_all();

        // 等待處理線程結束
        if (processingThread.joinable()) {
            processingThread.join();
        }

        std::cout << "Real-time echo cancellation stopped." << std::endl;
        std::cout << "Processed frames: " << processedFrames << std::endl;
        std::cout << "Dropped frames: " << droppedFrames << std::endl;
    }

    void cleanup() {
        stop();

        // 關閉流
        if (micStream) {
            Pa_CloseStream(micStream);
            micStream = nullptr;
        }
        if (speakerStream) {
            Pa_CloseStream(speakerStream);
            speakerStream = nullptr;
        }
        if (outputStream) {
            Pa_CloseStream(outputStream);
            outputStream = nullptr;
        }

        // 清理 Speex
        if (echoState) {
            speex_echo_state_destroy(echoState);
            echoState = nullptr;
        }
        if (preprocessState) {
            speex_preprocess_state_destroy(preprocessState);
            preprocessState = nullptr;
        }
        if (micResampler) {
            speex_resampler_destroy(micResampler);
            micResampler = nullptr;
        }
        if (speakerResampler) {
            speex_resampler_destroy(speakerResampler);
            speakerResampler = nullptr;
        }

        // 終止 PortAudio
        Pa_Terminate();
    }

    void setEchoMode(EchoCancelMode mode) {
        echoMode = mode;
        std::cout << "Echo cancellation mode set to: " << mode << std::endl;
    }
};

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Real-Time Echo Cancellation Demo" << std::endl;
    std::cout << "Based on Mumble's Echo Cancellation Design" << std::endl;
    std::cout << "=========================================" << std::endl;

    RealTimeEchoCancellation ec;

    if (!ec.initialize()) {
        std::cerr << "Failed to initialize echo cancellation" << std::endl;
        return -1;
    }

    if (!ec.start()) {
        std::cerr << "Failed to start echo cancellation" << std::endl;
        return -1;
    }

    // 等待用戶輸入停止
    std::cin.get();

    ec.stop();

    return 0;
}