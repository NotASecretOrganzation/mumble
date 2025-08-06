//#include <iostream>
//#include <vector>
//#include <portaudio.h>
//#include <speex/speex_echo.h>
//#include <speex/speex_preprocess.h>
//
//// Simple test using Mumble's exact echo cancellation code
//class SimpleMumbleEchoTest {
//private:
//    // Mumble's exact parameters
//    static const int SAMPLE_RATE = 48000;  // Mumble uses 48kHz
//    static const int FRAME_SIZE = 480;     // 10ms at 48kHz = 480 samples
//    static const int FILTER_LENGTH = 4800; // 100ms filter = 4800 samples
//
//    // PortAudio streams
//    PaStream* micStream;
//    PaStream* outputStream;
//
//    // Speex components (exactly like Mumble)
//    SpeexEchoState* sesEcho;
//    SpeexPreprocessState* preprocessState;
//
//    // Audio buffers
//    std::vector<short> micBuffer;
//    std::vector<short> speakerBuffer;
//    std::vector<short> outputBuffer;
//
//    // Statistics
//    int processedFrames = 0;
//    int droppedFrames = 0;
//
//public:
//    SimpleMumbleEchoTest() : 
//        micStream(nullptr), 
//        outputStream(nullptr),
//        sesEcho(nullptr),
//        preprocessState(nullptr) {
//        
//        micBuffer.resize(FRAME_SIZE);
//        speakerBuffer.resize(FRAME_SIZE);
//        outputBuffer.resize(FRAME_SIZE);
//    }
//
//    ~SimpleMumbleEchoTest() {
//        cleanup();
//    }
//
//    bool initialize() {
//        std::cout << "Initializing Simple Mumble Echo Test..." << std::endl;
//
//        // Initialize PortAudio
//        PaError err = Pa_Initialize();
//        if (err != paNoError) {
//            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // Initialize Speex exactly like Mumble does
//        if (!initializeSpeex()) {
//            return false;
//        }
//
//        // Setup audio streams
//        if (!setupAudioStreams()) {
//            return false;
//        }
//
//        std::cout << "Initialization completed successfully!" << std::endl;
//        std::cout << "Using Mumble's exact echo cancellation parameters:" << std::endl;
//        std::cout << "  Sample Rate: " << SAMPLE_RATE << "Hz" << std::endl;
//        std::cout << "  Frame Size: " << FRAME_SIZE << " samples (10ms)" << std::endl;
//        std::cout << "  Filter Length: " << FILTER_LENGTH << " samples (100ms)" << std::endl;
//
//        return true;
//    }
//
//private:
//    bool initializeSpeex() {
//        // Initialize echo cancellation state - EXACTLY like Mumble
//        sesEcho = speex_echo_state_init(FRAME_SIZE, FILTER_LENGTH);
//        if (!sesEcho) {
//            std::cerr << "Failed to initialize Speex echo state" << std::endl;
//            return false;
//        }
//
//        // Set sampling rate - EXACTLY like Mumble
//        int sampleRate = SAMPLE_RATE;
//        speex_echo_ctl(sesEcho, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
//
//        // Initialize preprocessor - EXACTLY like Mumble
//        preprocessState = speex_preprocess_state_init(FRAME_SIZE, SAMPLE_RATE);
//        if (!preprocessState) {
//            std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
//            return false;
//        }
//
//        // Associate echo cancellation and preprocessor - EXACTLY like Mumble
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, sesEcho);
//
//        // Configure preprocessor - EXACTLY like Mumble's settings
//        int denoise = 1;
//        int agc = 1;
//        int vad = 0;  // Disable VAD to avoid warnings
//        int agcLevel = 8000;
//        int agcMaxGain = 20000;
//        int agcIncrement = 12;
//        int agcDecrement = -40;
//        
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC, &agc);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_VAD, &vad);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_TARGET, &agcLevel);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &agcMaxGain);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &agcIncrement);
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &agcDecrement);
//
//        std::cout << "Speex initialized with Mumble's exact settings" << std::endl;
//        return true;
//    }
//
//    bool setupAudioStreams() {
//        // Setup microphone input stream
//        PaStreamParameters micParams;
//        micParams.device = Pa_GetDefaultInputDevice();
//        if (micParams.device == paNoDevice) {
//            std::cerr << "No default input device found" << std::endl;
//            return false;
//        }
//
//        micParams.channelCount = 1;  // Mono like Mumble
//        micParams.sampleFormat = paInt16;
//        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
//        micParams.hostApiSpecificStreamInfo = nullptr;
//
//        // Setup output stream
//        PaStreamParameters outputParams;
//        outputParams.device = Pa_GetDefaultOutputDevice();
//        if (outputParams.device == paNoDevice) {
//            std::cerr << "No default output device found" << std::endl;
//            return false;
//        }
//
//        outputParams.channelCount = 1;  // Mono like Mumble
//        outputParams.sampleFormat = paInt16;
//        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
//        outputParams.hostApiSpecificStreamInfo = nullptr;
//
//        // Open microphone stream
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
//        // Open output stream
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
//    // Microphone callback
//    static int micCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        SimpleMumbleEchoTest* test = static_cast<SimpleMumbleEchoTest*>(userData);
//        const short* input = static_cast<const short*>(inputBuffer);
//
//        if (input) {
//            // Copy microphone data
//            std::copy(input, input + framesPerBuffer, test->micBuffer.begin());
//            
//            // Process with echo cancellation (using dummy speaker data for testing)
//            test->processEchoCancellation();
//        }
//
//        return paContinue;
//    }
//
//    // Output callback
//    static int outputCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        SimpleMumbleEchoTest* test = static_cast<SimpleMumbleEchoTest*>(userData);
//        short* output = static_cast<short*>(outputBuffer);
//
//        // Output the processed audio
//        std::copy(test->outputBuffer.begin(), test->outputBuffer.end(), output);
//
//        return paContinue;
//    }
//
//    // Mumble's exact echo cancellation processing
//    void processEchoCancellation() {
//        // Create dummy speaker data (silence) for testing
//        std::fill(speakerBuffer.begin(), speakerBuffer.end(), 0);
//
//        // MUMBLE'S EXACT ECHO CANCELLATION CODE:
//        short psClean[FRAME_SIZE];
//        if (sesEcho) {
//            // This is Mumble's exact speex_echo_cancellation call
//            speex_echo_cancellation(sesEcho, micBuffer.data(), speakerBuffer.data(), psClean);
//            
//            // Copy to output buffer
//            std::copy(psClean, psClean + FRAME_SIZE, outputBuffer.begin());
//        } else {
//            // No echo cancellation, use microphone data directly
//            std::copy(micBuffer.begin(), micBuffer.end(), outputBuffer.begin());
//        }
//
//        // Apply preprocessing (exactly like Mumble)
//        if (preprocessState) {
//            speex_preprocess_run(preprocessState, outputBuffer.data());
//        }
//
//        processedFrames++;
//        
//        // Log every 100 frames
//        if (processedFrames % 100 == 0) {
//            std::cout << "Processed " << processedFrames << " frames with Mumble's echo cancellation" << std::endl;
//        }
//    }
//
//    bool start() {
//        std::cout << "Starting simple Mumble echo test..." << std::endl;
//
//        // Start microphone stream
//        PaError err = Pa_StartStream(micStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // Start output stream
//        err = Pa_StartStream(outputStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
//            Pa_StopStream(micStream);
//            return false;
//        }
//
//        std::cout << "Simple Mumble echo test started successfully!" << std::endl;
//        std::cout << "Note: Using dummy speaker data (silence) for testing" << std::endl;
//        std::cout << "Press Enter to stop..." << std::endl;
//
//        return true;
//    }
//
//    void stop() {
//        std::cout << "Stopping simple Mumble echo test..." << std::endl;
//
//        if (micStream) Pa_StopStream(micStream);
//        if (outputStream) Pa_StopStream(outputStream);
//
//        std::cout << "Simple Mumble echo test stopped." << std::endl;
//        std::cout << "Processed frames: " << processedFrames << std::endl;
//        std::cout << "Dropped frames: " << droppedFrames << std::endl;
//    }
//
//    void cleanup() {
//        stop();
//
//        // Close streams
//        if (micStream) {
//            Pa_CloseStream(micStream);
//            micStream = nullptr;
//        }
//        if (outputStream) {
//            Pa_CloseStream(outputStream);
//            outputStream = nullptr;
//        }
//
//        // Cleanup Speex (exactly like Mumble)
//        if (sesEcho) {
//            speex_echo_state_destroy(sesEcho);
//            sesEcho = nullptr;
//        }
//        if (preprocessState) {
//            speex_preprocess_state_destroy(preprocessState);
//            preprocessState = nullptr;
//        }
//
//        // Terminate PortAudio
//        Pa_Terminate();
//    }
//};
//
//int main() {
//    std::cout << "=========================================" << std::endl;
//    std::cout << "Simple Mumble Echo Cancellation Test" << std::endl;
//    std::cout << "Using Mumble's Exact Echo Cancellation Code" << std::endl;
//    std::cout << "=========================================" << std::endl;
//
//    SimpleMumbleEchoTest test;
//
//    if (!test.initialize()) {
//        std::cerr << "Failed to initialize echo cancellation test" << std::endl;
//        return -1;
//    }
//
//    if (!test.start()) {
//        std::cerr << "Failed to start echo cancellation test" << std::endl;
//        return -1;
//    }
//
//    // Wait for user input to stop
//    std::cin.get();
//
//    test.stop();
//
//    return 0;
//} 