//#include <iostream>
//#include <vector>
//#include <thread>
//#include <atomic>
//#include <mutex>
//#include <condition_variable>
//#include <queue>
//#include <deque>
//#include <chrono>
//#include <algorithm>
//#include <cmath>
//#include <portaudio.h>
//#include <speex/speex_echo.h>
//#include <speex/speex_preprocess.h>
//#include <speex/speex_resampler.h>
//
//// Based on Mumble's design pattern with state machine synchronization
//class RealTimeEchoCancellation {
//private:
//    // Audio parameters - based on Mumble's configuration
//    static const int TARGET_SAMPLE_RATE = 48000;  // Mumble uses 48kHz
//    static const int FRAME_SIZE_MS = 10;          // 10ms frame
//    static const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;  // 480 samples
//    static const int FILTER_LENGTH_MS = 100;      // 100ms filter length
//    static const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;
//    static const int CHANNELS = 1;                // Mono
//    static const int MAX_BUFFER_SIZE = 50;        // Maximum buffer size
//
//    // PortAudio streams
//    PaStream* micStream;
//    PaStream* speakerStream;
//    PaStream* outputStream;
//
//    // Speex components
//    SpeexEchoState* echoState;
//    SpeexPreprocessState* preprocessState;
//    SpeexResamplerState* micResampler;
//    SpeexResamplerState* speakerResampler;
//
//    // Mumble's state machine for synchronization
//    std::queue<std::vector<int16_t>> micQueue;
//    std::mutex syncMutex;
//    std::condition_variable syncCondition;
//    
//    // State machine states (0-5 like Mumble)
//    int state = 0;
//    
//    // Audio buffers
//    std::deque<std::vector<int16_t>> outputBuffer;
//    std::mutex outputMutex;
//    std::atomic<bool> running;
//
//    // Processing thread
//    std::thread processingThread;
//
//    // Audio device information
//    int micSampleRate;
//    int speakerSampleRate;
//    int micChannels;
//    int speakerChannels;
//
//    // Echo cancellation options - based on Mumble's design
//    enum EchoCancelMode {
//        ECHO_DISABLED = 0,
//        ECHO_MIXED = 1,
//        ECHO_MULTICHANNEL = 2
//    };
//    EchoCancelMode echoMode;
//
//    // Statistics
//    std::atomic<int64_t> processedFrames;
//    std::atomic<int64_t> droppedFrames;
//
//public:
//    RealTimeEchoCancellation() :
//        micStream(nullptr),
//        speakerStream(nullptr),
//        outputStream(nullptr),
//        echoState(nullptr),
//        preprocessState(nullptr),
//        micResampler(nullptr),
//        speakerResampler(nullptr),
//        running(false),
//        micSampleRate(0),
//        speakerSampleRate(0),
//        micChannels(0),
//        speakerChannels(0),
//        echoMode(ECHO_MIXED),
//        processedFrames(0),
//        droppedFrames(0) {
//    }
//
//    ~RealTimeEchoCancellation() {
//        cleanup();
//    }
//
//    bool initialize() {
//        std::cout << "Initializing Real-Time Echo Cancellation with Mumble State Machine..." << std::endl;
//
//        // Initialize PortAudio
//        PaError err = Pa_Initialize();
//        if (err != paNoError) {
//            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // Get device information
//        if (!getDeviceInfo()) {
//            return false;
//        }
//
//        // Initialize Speex echo cancellation
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
//        std::cout << "Mic: " << micSampleRate << "Hz, " << micChannels << " channels" << std::endl;
//        std::cout << "Speaker: " << speakerSampleRate << "Hz, " << speakerChannels << " channels" << std::endl;
//        std::cout << "Target: " << TARGET_SAMPLE_RATE << "Hz, " << CHANNELS << " channels" << std::endl;
//        std::cout << "Using Mumble's state machine for mic/speaker synchronization" << std::endl;
//
//        return true;
//    }
//
//private:
//    bool getDeviceInfo() {
//        // Get default input device
//        PaDeviceIndex micDevice = Pa_GetDefaultInputDevice();
//        if (micDevice == paNoDevice) {
//            std::cerr << "No default input device found" << std::endl;
//            return false;
//        }
//
//        const PaDeviceInfo* micInfo = Pa_GetDeviceInfo(micDevice);
//        if (!micInfo) {
//            std::cerr << "Failed to get input device info" << std::endl;
//            return false;
//        }
//
//        micSampleRate = static_cast<int>(micInfo->defaultSampleRate);
//        micChannels = micInfo->maxInputChannels;
//
//        // Get default output device
//        PaDeviceIndex speakerDevice = Pa_GetDefaultOutputDevice();
//        if (speakerDevice == paNoDevice) {
//            std::cerr << "No default output device found" << std::endl;
//            return false;
//        }
//
//        const PaDeviceInfo* speakerInfo = Pa_GetDeviceInfo(speakerDevice);
//        if (!speakerInfo) {
//            std::cerr << "Failed to get output device info" << std::endl;
//            return false;
//        }
//
//        speakerSampleRate = static_cast<int>(speakerInfo->defaultSampleRate);
//        speakerChannels = speakerInfo->maxOutputChannels;
//
//        return true;
//    }
//
//    bool initializeSpeex() {
//        // Initialize echo cancellation state - based on Mumble's configuration
//        echoState = speex_echo_state_init(FRAME_SIZE, FILTER_LENGTH);
//        if (!echoState) {
//            std::cerr << "Failed to initialize Speex echo state" << std::endl;
//            return false;
//        }
//
//        // Set sampling rate
//        int sampleRate = TARGET_SAMPLE_RATE;
//        speex_echo_ctl(echoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
//
//        // Initialize preprocessor
//        preprocessState = speex_preprocess_state_init(FRAME_SIZE, TARGET_SAMPLE_RATE);
//        if (!preprocessState) {
//            std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
//            return false;
//        }
//
//        // Associate echo cancellation and preprocessor - based on Mumble's design
//        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState);
//
//        // Configure preprocessor - based on Mumble's settings
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
//        // Initialize resamplers - based on Mumble's resampling strategy
//        int err = 0;
//        if (micSampleRate != TARGET_SAMPLE_RATE) {
//            micResampler = speex_resampler_init(1, micSampleRate, TARGET_SAMPLE_RATE, 3, &err);
//            if (err != 0) {
//                std::cerr << "Failed to initialize mic resampler" << std::endl;
//                return false;
//            }
//        }
//
//        if (speakerSampleRate != TARGET_SAMPLE_RATE) {
//            speakerResampler = speex_resampler_init(1, speakerSampleRate, TARGET_SAMPLE_RATE, 3, &err);
//            if (err != 0) {
//                std::cerr << "Failed to initialize speaker resampler" << std::endl;
//                return false;
//            }
//        }
//
//        return true;
//    }
//
//    bool setupAudioStreams() {
//        // Setup microphone input stream
//        PaStreamParameters micParams;
//        micParams.device = Pa_GetDefaultInputDevice();
//        micParams.channelCount = micChannels;
//        micParams.sampleFormat = paInt16;
//        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
//        micParams.hostApiSpecificStreamInfo = nullptr;
//
//        // Setup speaker monitoring stream (try using loopback mode)
//        PaStreamParameters speakerParams;
//        speakerParams.device = Pa_GetDefaultOutputDevice();
//        speakerParams.channelCount = speakerChannels;
//        speakerParams.sampleFormat = paInt16;
//        speakerParams.suggestedLatency = Pa_GetDeviceInfo(speakerParams.device)->defaultLowOutputLatency;
//        speakerParams.hostApiSpecificStreamInfo = nullptr;
//
//        // Setup output stream
//        PaStreamParameters outputParams;
//        outputParams.device = Pa_GetDefaultOutputDevice();
//        outputParams.channelCount = CHANNELS;
//        outputParams.sampleFormat = paInt16;
//        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
//        outputParams.hostApiSpecificStreamInfo = nullptr;
//
//        // Open microphone stream
//        PaError err = Pa_OpenStream(&micStream,
//            &micParams,
//            nullptr,
//            micSampleRate,
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
//        // Try to open speaker monitoring stream (using loopback)
//        // Note: Loopback mode may not be supported in some PortAudio versions
//        err = Pa_OpenStream(&speakerStream,
//            &speakerParams,
//            nullptr,
//            speakerSampleRate,
//            FRAME_SIZE,
//            paClipOff,  // Remove paLoopback flag
//            speakerCallback,
//            this);
//
//        if (err != paNoError) {
//            std::cerr << "Failed to open speaker stream: " << Pa_GetErrorText(err) << std::endl;
//            std::cerr << "Trying alternative approach..." << std::endl;
//            
//            // Try using mono
//            speakerParams.channelCount = 1;
//            err = Pa_OpenStream(&speakerStream,
//                &speakerParams,
//                nullptr,
//                speakerSampleRate,
//                FRAME_SIZE,
//                paClipOff,  // Remove paLoopback flag
//                speakerCallback,
//                this);
//                
//            if (err != paNoError) {
//                std::cerr << "Failed to open speaker stream with mono: " << Pa_GetErrorText(err) << std::endl;
//                std::cerr << "Echo cancellation will be disabled (no speaker monitoring)" << std::endl;
//                
//                // If speaker stream completely fails, disable echo cancellation
//                echoMode = ECHO_DISABLED;
//                speakerStream = nullptr;
//            } else {
//                std::cout << "Speaker stream opened with mono" << std::endl;
//                speakerChannels = 1;  // Update to mono
//            }
//        } else {
//            std::cout << "Speaker stream opened successfully" << std::endl;
//        }
//
//        // Open output stream
//        err = Pa_OpenStream(&outputStream,
//            nullptr,
//            &outputParams,
//            TARGET_SAMPLE_RATE,
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
//    // Microphone callback - using Mumble's AddMic approach
//    static int micCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
//        const int16_t* input = static_cast<const int16_t*>(inputBuffer);
//
//        if (input) {
//            std::vector<int16_t> frame(input, input + framesPerBuffer * ec->micChannels);
//            
//            // Use Mumble's AddMic approach
//            ec->addMic(frame);
//        }
//
//        return paContinue;
//    }
//
//    // Speaker callback - using Mumble's AddSpeaker approach
//    static int speakerCallback(const void* inputBuffer, void* outputBuffer,
//        unsigned long framesPerBuffer,
//        const PaStreamCallbackTimeInfo* timeInfo,
//        PaStreamCallbackFlags statusFlags,
//        void* userData) {
//
//        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
//        const int16_t* input = static_cast<const int16_t*>(inputBuffer);
//
//        if (input) {
//            std::vector<int16_t> frame(input, input + framesPerBuffer * ec->speakerChannels);
//            
//            // Use Mumble's AddSpeaker approach
//            ec->addSpeaker(frame);
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
//        RealTimeEchoCancellation* ec = static_cast<RealTimeEchoCancellation*>(userData);
//        int16_t* output = static_cast<int16_t*>(outputBuffer);
//
//        std::unique_lock<std::mutex> lock(ec->outputMutex);
//
//        if (!ec->outputBuffer.empty()) {
//            std::vector<int16_t> frame = ec->outputBuffer.front();
//            ec->outputBuffer.pop_front();
//            lock.unlock();
//
//            std::copy(frame.begin(), frame.end(), output);
//        } else {
//            // Output silence
//            std::fill(output, output + framesPerBuffer, 0);
//        }
//
//        return paContinue;
//    }
//
//    // Mumble's AddMic state machine approach
//    void addMic(const std::vector<int16_t>& micData) {
//        bool drop = false;
//        
//        {
//            std::lock_guard<std::mutex> lock(syncMutex);
//            micQueue.push(micData);
//            
//            // Mumble's simple state machine: 0->1->2->3->4->5 (drop)
//            if (state < 5) {
//                state++;
//            } else {
//                drop = true;
//                micQueue.pop(); // Remove the frame we just added
//            }
//        }
//        
//        if (drop) {
//            droppedFrames++;
//        }
//        
//        syncCondition.notify_one();
//    }
//
//    // Mumble's AddSpeaker state machine approach
//    void addSpeaker(const std::vector<int16_t>& speakerData) {
//        // This method is called when speaker data arrives
//        // The actual synchronization happens in the processing loop
//        // We just store the speaker data for later use
//        // In a real implementation, you might want to store this in a separate queue
//    }
//
//    // Audio processing using Mumble's state machine
//    void processAudio() {
//        std::vector<int16_t> micFrame(FRAME_SIZE);
//        std::vector<int16_t> speakerFrame(FRAME_SIZE);
//        std::vector<int16_t> outputFrame(FRAME_SIZE);
//        std::vector<int16_t> resampledMicFrame;
//        std::vector<int16_t> resampledSpeakerFrame;
//        
//        // Create dummy speaker data for testing (silence)
//        std::vector<int16_t> dummySpeakerData(FRAME_SIZE, 0);
//
//        std::cout << "Audio processing started with Mumble state machine" << std::endl;
//        std::cout << "Echo cancellation mode: " << (echoMode == ECHO_DISABLED ? "Disabled" : "Enabled") << std::endl;
//        std::cout << "Note: Using dummy speaker data for echo cancellation testing" << std::endl;
//
//        while (running) {
//            // Wait for microphone data
//            std::unique_lock<std::mutex> lock(syncMutex);
//            syncCondition.wait(lock, [this] { return !micQueue.empty() || !running; });
//
//            if (!running) break;
//
//            if (!micQueue.empty()) {
//                // Get synchronized pair using Mumble's approach
//                std::pair<std::vector<int16_t>, std::vector<int16_t>> pair = addSpeakerToQueue(dummySpeakerData);
//                std::vector<int16_t> rawMicFrame = pair.first;
//                std::vector<int16_t> rawSpeakerFrame = pair.second;
//                
//                if (!rawMicFrame.empty() && !rawSpeakerFrame.empty()) {
//                    // Resample microphone data - based on Mumble's resampling logic
//                    if (micResampler) {
//                        resampledMicFrame = resampleAudio(rawMicFrame, micResampler, micChannels);
//                    } else {
//                        resampledMicFrame = rawMicFrame;
//                    }
//
//                    // Resample speaker data
//                    if (speakerResampler && !rawSpeakerFrame.empty()) {
//                        resampledSpeakerFrame = resampleAudio(rawSpeakerFrame, speakerResampler, speakerChannels);
//                    } else {
//                        resampledSpeakerFrame = rawSpeakerFrame;
//                    }
//
//                    // Mix multi-channel to mono - based on Mumble's inMixerFunc
//                    if (micChannels > 1) {
//                        resampledMicFrame = mixToMono(resampledMicFrame, micChannels);
//                    }
//                    if (speakerChannels > 1) {
//                        resampledSpeakerFrame = mixToMono(resampledSpeakerFrame, speakerChannels);
//                    }
//
//                    // Ensure frame size is correct
//                    if (resampledMicFrame.size() != FRAME_SIZE) {
//                        resampledMicFrame.resize(FRAME_SIZE, 0);
//                    }
//                    if (resampledSpeakerFrame.size() != FRAME_SIZE) {
//                        resampledSpeakerFrame.resize(FRAME_SIZE, 0);
//                    }
//
//                    // Perform echo cancellation - based on Mumble's speex_echo_cancellation call
//                    if (echoMode != ECHO_DISABLED && speakerStream) {
//                        speex_echo_cancellation(echoState,
//                            resampledSpeakerFrame.data(),
//                            resampledMicFrame.data(),
//                            outputFrame.data());
//                    } else {
//                        // If echo cancellation is disabled or no speaker stream, use microphone data directly
//                        std::copy(resampledMicFrame.begin(), resampledMicFrame.end(), outputFrame.begin());
//                    }
//
//                    // Apply preprocessing - based on Mumble's speex_preprocess_run
//                    speex_preprocess_run(preprocessState, outputFrame.data());
//
//                    // Add to output buffer
//                    std::lock_guard<std::mutex> outputLock(outputMutex);
//                    outputBuffer.push_back(outputFrame);
//
//                    // Limit output buffer size
//                    while (outputBuffer.size() > MAX_BUFFER_SIZE) {
//                        outputBuffer.pop_front();
//                    }
//
//                    processedFrames++;
//                    
//                    // Log every 100 frames
//                    if (processedFrames % 100 == 0) {
//                        std::cout << "Processed " << processedFrames << " frames - "
//                                  << "Dropped: " << droppedFrames << " - "
//                                  << "Queue: " << micQueue.size() << " - "
//                                  << "State: " << state << std::endl;
//                    }
//                }
//            }
//        }
//    }
//
//private:
//    // Mumble's AddSpeaker state machine approach for processing
//    std::pair<std::vector<int16_t>, std::vector<int16_t>> addSpeakerToQueue(const std::vector<int16_t>& speakerData) {
//        std::vector<int16_t> micData;
//        bool drop = false;
//        
//        {
//            std::lock_guard<std::mutex> lock(syncMutex);
//            
//            // Mumble's simple state machine: 5->4->3->2->1->0 (drop)
//            if (state > 0) {
//                state--;
//                if (!micQueue.empty()) {
//                    micData = micQueue.front();
//                    micQueue.pop();
//                }
//            } else {
//                drop = true;
//            }
//        }
//        
//        if (drop) {
//            droppedFrames++;
//            return {{}, {}}; // Return empty pair
//        }
//        
//        return {micData, speakerData};
//    }
//
//    // Resampling function - based on Mumble's speex_resampler_process_interleaved_int
//    std::vector<int16_t> resampleAudio(const std::vector<int16_t>& input, SpeexResamplerState* resampler, int channels) {
//        std::vector<int16_t> output;
//        
//        if (!resampler) return input;
//
//        // Calculate output size
//        spx_uint32_t inLen = static_cast<spx_uint32_t>(input.size() / channels);
//        spx_uint32_t outLen = static_cast<spx_uint32_t>((inLen * TARGET_SAMPLE_RATE) / 
//                                                       (channels == 1 ? micSampleRate : speakerSampleRate));
//
//        output.resize(outLen * channels);
//
//        speex_resampler_process_interleaved_int(resampler, 
//            input.data(), &inLen, 
//            output.data(), &outLen);
//
//        output.resize(outLen * channels);
//        return output;
//    }
//
//    // Channel mixing function - based on Mumble's inMixerFloat macro
//    std::vector<int16_t> mixToMono(const std::vector<int16_t>& input, int channels) {
//        if (channels == 1) return input;
//
//        std::vector<int16_t> output;
//        output.reserve(input.size() / channels);
//
//        for (size_t i = 0; i < input.size(); i += channels) {
//            int32_t sum = 0;
//            for (int ch = 0; ch < channels; ch++) {
//                sum += input[i + ch];
//            }
//            output.push_back(static_cast<int16_t>(sum / channels));
//        }
//
//        return output;
//    }
//
//public:
//    bool start() {
//        if (running) return false;
//
//        std::cout << "Starting real-time echo cancellation with Mumble state machine..." << std::endl;
//
//        running = true;
//
//        // Start processing thread
//        processingThread = std::thread(&RealTimeEchoCancellation::processAudio, this);
//
//        // Start microphone stream
//        PaError err = Pa_StartStream(micStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            return false;
//        }
//
//        // Start speaker stream (if available)
//        if (speakerStream) {
//            err = Pa_StartStream(speakerStream);
//            if (err != paNoError) {
//                std::cerr << "Failed to start speaker stream: " << Pa_GetErrorText(err) << std::endl;
//                std::cerr << "Continuing without speaker monitoring..." << std::endl;
//                echoMode = ECHO_DISABLED;
//            } else {
//                std::cout << "Speaker monitoring started successfully" << std::endl;
//            }
//        } else {
//            std::cout << "No speaker stream available - echo cancellation disabled" << std::endl;
//        }
//
//        // Start output stream
//        err = Pa_StartStream(outputStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            Pa_StopStream(micStream);
//            if (speakerStream) Pa_StopStream(speakerStream);
//            return false;
//        }
//
//        std::cout << "Real-time echo cancellation started successfully!" << std::endl;
//        std::cout << "Using Mumble's state machine for synchronization" << std::endl;
//        std::cout << "Status: " << (echoMode == ECHO_DISABLED ? "Echo cancellation disabled" : "Echo cancellation enabled") << std::endl;
//        std::cout << "Press Enter to stop..." << std::endl;
//
//        return true;
//    }
//
//    void stop() {
//        if (!running) return;
//
//        std::cout << "Stopping real-time echo cancellation..." << std::endl;
//
//        running = false;
//
//        // Stop audio streams
//        if (micStream) Pa_StopStream(micStream);
//        if (speakerStream) Pa_StopStream(speakerStream);
//        if (outputStream) Pa_StopStream(outputStream);
//
//        // Wake up processing thread
//        syncCondition.notify_all();
//
//        // Wait for processing thread to end
//        if (processingThread.joinable()) {
//            processingThread.join();
//        }
//
//        std::cout << "Real-time echo cancellation stopped." << std::endl;
//        std::cout << "Processed frames: " << processedFrames << std::endl;
//        std::cout << "Dropped frames: " << droppedFrames << std::endl;
//        std::cout << "Final state: " << state << std::endl;
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
//        if (speakerStream) {
//            Pa_CloseStream(speakerStream);
//            speakerStream = nullptr;
//        }
//        if (outputStream) {
//            Pa_CloseStream(outputStream);
//            outputStream = nullptr;
//        }
//
//        // Cleanup Speex
//        if (echoState) {
//            speex_echo_state_destroy(echoState);
//            echoState = nullptr;
//        }
//        if (preprocessState) {
//            speex_preprocess_state_destroy(preprocessState);
//            preprocessState = nullptr;
//        }
//        if (micResampler) {
//            speex_resampler_destroy(micResampler);
//            micResampler = nullptr;
//        }
//        if (speakerResampler) {
//            speex_resampler_destroy(speakerResampler);
//            speakerResampler = nullptr;
//        }
//
//        // Terminate PortAudio
//        Pa_Terminate();
//    }
//
//    void setEchoMode(EchoCancelMode mode) {
//        echoMode = mode;
//        std::cout << "Echo cancellation mode set to: " << mode << std::endl;
//    }
//};
//
//int main() {
//    std::cout << "=========================================" << std::endl;
//    std::cout << "Real-Time Echo Cancellation Demo" << std::endl;
//    std::cout << "Using Mumble's State Machine for Synchronization" << std::endl;
//    std::cout << "=========================================" << std::endl;
//
//    RealTimeEchoCancellation ec;
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
//    // Wait for user input to stop
//    std::cin.get();
//
//    ec.stop();
//
//    return 0;
//}