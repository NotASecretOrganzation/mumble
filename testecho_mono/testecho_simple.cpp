//#include <iostream>
//#include <vector>
//#include <thread>
//#include <atomic>
//#include <mutex>
//#include <condition_variable>
//#include <queue>
//#include <portaudio.h>
//#include <speex/speex_echo.h>
//#include <speex/speex_preprocess.h>
//
//// 簡化版本的實時回音消除
//class SimpleEchoCancellation {
//private:
//    // 音頻參數
//    static const int SAMPLE_RATE = 48000;
//    static const int FRAME_SIZE = 480;  // 10ms at 48kHz
//    static const int FILTER_LENGTH = 4800;  // 100ms
//    static const int CHANNELS = 1;
//
//    // PortAudio 流
//    PaStream* micStream;
//    PaStream* outputStream;
//
//    // Speex 組件
//    SpeexEchoState* echoState;
//    SpeexPreprocessState* preprocessState;
//
//    // 音頻緩衝區
//    std::queue<std::vector<int16_t>> micBuffer;
//    std::queue<std::vector<int16_t>> outputBuffer;
//
//    // 同步
//    std::mutex micMutex;
//    std::mutex outputMutex;
//    std::condition_variable micCondition;
//    std::atomic<bool> running;
//
//    // 處理線程
//    std::thread processingThread;
//
//    // 模擬揚聲器數據（用於測試）
//    std::vector<int16_t> dummySpeakerData;
//
//public:
//    SimpleEchoCancellation() :
//        micStream(nullptr),
//        outputStream(nullptr),
//        echoState(nullptr),
//        preprocessState(nullptr),
//        running(false) {
//        
//        // 創建模擬揚聲器數據（靜音）
//        dummySpeakerData.resize(FRAME_SIZE, 0);
//    }
//
//    ~SimpleEchoCancellation() {
//        cleanup();
//    }
//
//    bool initialize() {
//        std::cout << "Initializing Simple Echo Cancellation..." << std::endl;
//
//        // 初始化 PortAudio
//        PaError err = Pa_Initialize();
//        if (err != paNoError) {
//            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // 初始化 Speex
//        if (!initializeSpeex()) {
//            return false;
//        }
//
//        // 設置音頻流
//        if (!setupAudioStreams()) {
//            return false;
//        }
//
//        std::cout << "Initialization completed successfully!" << std::endl;
//        return true;
//    }
//
//private:
//    bool initializeSpeex() {
//        // 初始化回音消除狀態
//        echoState = speex_echo_state_init(FRAME_SIZE, FILTER_LENGTH);
//        if (!echoState) {
//            std::cerr << "Failed to initialize Speex echo state" << std::endl;
//            return false;
//        }
//
//        // 設置採樣率
//        int sampleRate = SAMPLE_RATE;
//        speex_echo_ctl(echoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
//
//        // 初始化預處理器
//        preprocessState = speex_preprocess_state_init(FRAME_SIZE, SAMPLE_RATE);
//        if (!preprocessState) {
//            std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
//            return false;
//        }
//
//        // 關聯回音消除和預處理
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState);
//
//        // 配置預處理器
//        int denoise = 1;
//        int agc = 1;
//        int vad = 0;  // 禁用 VAD 以避免警告
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC, &agc);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_VAD, &vad);
//
//        return true;
//    }
//
//    bool setupAudioStreams() {
//        // 設置麥克風輸入流
//        PaStreamParameters micParams;
//        micParams.device = Pa_GetDefaultInputDevice();
//        if (micParams.device == paNoDevice) {
//            std::cerr << "No default input device found" << std::endl;
//            return false;
//        }
//
//        micParams.channelCount = CHANNELS;
//        micParams.sampleFormat = paInt16;
//        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
//        micParams.hostApiSpecificStreamInfo = nullptr;
//
//        // 設置輸出流
//        PaStreamParameters outputParams;
//        outputParams.device = Pa_GetDefaultOutputDevice();
//        if (outputParams.device == paNoDevice) {
//            std::cerr << "No default output device found" << std::endl;
//            return false;
//        }
//
//        outputParams.channelCount = CHANNELS;
//        outputParams.sampleFormat = paInt16;
//        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
//        outputParams.hostApiSpecificStreamInfo = nullptr;
//
//        // 打開麥克風流
//        PaError err = Pa_OpenStream(&micStream,
//            &micParams,
//            nullptr,
//            SAMPLE_RATE,
//            FRAME_SIZE,
//            paClipOff,
//            micCallback,
//            this);
//
//        if (err != paNoError) {
//            std::cerr << "Failed to open mic stream: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // 打開輸出流
//        err = Pa_OpenStream(&outputStream,
//            nullptr,
//            &outputParams,
//            SAMPLE_RATE,
//            FRAME_SIZE,
//            paClipOff,
//            outputCallback,
//            this);
//
//        if (err != paNoError) {
//            std::cerr << "Failed to open output stream: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        return true;
//    }
//
//public:
//    // 麥克風回調
//    static int micCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        SimpleEchoCancellation* ec = static_cast<SimpleEchoCancellation*>(userData);
//        const int16_t* input = static_cast<const int16_t*>(inputBuffer);
//
//        if (input) {
//            std::vector<int16_t> frame(input, input + framesPerBuffer);
//
//            std::lock_guard<std::mutex> lock(ec->micMutex);
//            ec->micBuffer.push(frame);
//            ec->micCondition.notify_one();
//        }
//
//        return paContinue;
//    }
//
//    // 輸出回調
//    static int outputCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        SimpleEchoCancellation* ec = static_cast<SimpleEchoCancellation*>(userData);
//        int16_t* output = static_cast<int16_t*>(outputBuffer);
//
//        std::unique_lock<std::mutex> lock(ec->outputMutex);
//
//        if (!ec->outputBuffer.empty()) {
//            std::vector<int16_t> frame = ec->outputBuffer.front();
//            ec->outputBuffer.pop();
//            lock.unlock();
//
//            std::copy(frame.begin(), frame.end(), output);
//        } else {
//            // 輸出靜音
//            std::fill(output, output + framesPerBuffer, 0);
//        }
//
//        return paContinue;
//    }
//
//    void processAudio() {
//        std::vector<int16_t> micFrame(FRAME_SIZE);
//        std::vector<int16_t> outputFrame(FRAME_SIZE);
//
//        std::cout << "Audio processing started (echo cancellation with dummy speaker data)" << std::endl;
//
//        while (running) {
//            // 等待麥克風數據
//            std::unique_lock<std::mutex> micLock(micMutex);
//            micCondition.wait(micLock, [this] { return !micBuffer.empty() || !running; });
//
//            if (!running) break;
//
//            if (!micBuffer.empty()) {
//                micFrame = micBuffer.front();
//                micBuffer.pop();
//                micLock.unlock();
//
//                // 執行回音消除（使用模擬揚聲器數據）
//                speex_echo_cancellation(echoState,
//                    dummySpeakerData.data(),
//                    micFrame.data(),
//                    outputFrame.data());
//
//                // 應用預處理
//                speex_preprocess_run(preprocessState, outputFrame.data());
//
//                // 添加到輸出緩衝區
//                std::lock_guard<std::mutex> outputLock(outputMutex);
//                outputBuffer.push(outputFrame);
//            }
//        }
//    }
//
//    bool start() {
//        if (running) return false;
//
//        std::cout << "Starting simple echo cancellation..." << std::endl;
//
//        running = true;
//
//        // 啟動處理線程
//        processingThread = std::thread(&SimpleEchoCancellation::processAudio, this);
//
//        // 啟動音頻流
//        PaError err = Pa_StartStream(micStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            return false;
//        }
//
//        err = Pa_StartStream(outputStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            Pa_StopStream(micStream);
//            return false;
//        }
//
//        std::cout << "Simple echo cancellation started successfully!" << std::endl;
//        std::cout << "Note: Using dummy speaker data for testing" << std::endl;
//        std::cout << "Press Enter to stop..." << std::endl;
//
//        return true;
//    }
//
//    void stop() {
//        if (!running) return;
//
//        std::cout << "Stopping simple echo cancellation..." << std::endl;
//
//        running = false;
//
//        // 停止音頻流
//        if (micStream) Pa_StopStream(micStream);
//        if (outputStream) Pa_StopStream(outputStream);
//
//        // 喚醒處理線程
//        micCondition.notify_all();
//
//        // 等待處理線程結束
//        if (processingThread.joinable()) {
//            processingThread.join();
//        }
//
//        std::cout << "Simple echo cancellation stopped." << std::endl;
//    }
//
//    void cleanup() {
//        stop();
//
//        // 關閉流
//        if (micStream) {
//            Pa_CloseStream(micStream);
//            micStream = nullptr;
//        }
//        if (outputStream) {
//            Pa_CloseStream(outputStream);
//            outputStream = nullptr;
//        }
//
//        // 清理 Speex
//        if (echoState) {
//            speex_echo_state_destroy(echoState);
//            echoState = nullptr;
//        }
//        if (preprocessState) {
//            speex_preprocess_state_destroy(preprocessState);
//            preprocessState = nullptr;
//        }
//
//        // 終止 PortAudio
//        Pa_Terminate();
//    }
//};
//
//int main() {
//    std::cout << "=========================================" << std::endl;
//    std::cout << "Simple Real-Time Echo Cancellation Demo" << std::endl;
//    std::cout << "=========================================" << std::endl;
//
//    SimpleEchoCancellation ec;
//
//    if (!ec.initialize()) {
//        std::cerr << "Failed to initialize echo cancellation" << std::endl;
//        return -1;
//    }
//
//    if (!ec.start()) {
//        std::cerr << "Failed to start echo cancellation" << std::endl;
//        return -1;
//    }
//
//    // 等待用戶輸入停止
//    std::cin.get();
//
//    ec.stop();
//
//    return 0;
//} 