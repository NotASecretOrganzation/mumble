#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <portaudio.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

// Basic real-time echo cancellation example using Mumble's state machine
class BasicEchoCancellation {
private:
    // Audio parameters - based on Mumble's configuration
    static const int TARGET_SAMPLE_RATE = 48000;  // Mumble uses 48kHz
    static const int FRAME_SIZE_MS = 10;          // 10ms frame
    static const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;  // 480 samples
    static const int FILTER_LENGTH_MS = 100;      // 100ms filter length
    static const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;
    static const int CHANNELS = 1;                // Mono
    static const int MAX_BUFFER_SIZE = 50;        // Maximum buffer size

    // PortAudio streams
    PaStream* micStream;
    PaStream* outputStream;

    // Speex components
    SpeexEchoState* echoState;
    SpeexPreprocessState* preprocessState;

    // Mumble's state machine for synchronization
    std::queue<std::vector<int16_t>> micQueue;
    std::mutex syncMutex;
    std::condition_variable syncCondition;
    
    // State machine states (0-5 like Mumble)
    int state = 0;
    
    // Audio buffers
    std::queue<std::vector<int16_t>> outputBuffer;
    std::mutex outputMutex;
    std::atomic<bool> running;

    // Processing thread
    std::thread processingThread;

    // Statistics
    std::atomic<int64_t> processedFrames;
    std::atomic<int64_t> droppedFrames;

public:
    BasicEchoCancellation() :
        micStream(nullptr),
        outputStream(nullptr),
        echoState(nullptr),
        preprocessState(nullptr),
        running(false),
        processedFrames(0),
        droppedFrames(0) {
    }

    ~BasicEchoCancellation() {
        cleanup();
    }

    bool initialize() {
        std::cout << "Initializing Basic Echo Cancellation with Mumble State Machine..." << std::endl;

        // Initialize PortAudio
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // Initialize Speex
        if (!initializeSpeex()) {
            return false;
        }

        // Setup audio streams
        if (!setupAudioStreams()) {
            return false;
        }

        std::cout << "Initialization completed successfully!" << std::endl;
        std::cout << "Target: " << TARGET_SAMPLE_RATE << "Hz, " << CHANNELS << " channels" << std::endl;
        std::cout << "Using Mumble's state machine for mic/speaker synchronization" << std::endl;

        return true;
    }

private:
    bool initializeSpeex() {
        // Initialize echo cancellation state - based on Mumble's configuration
        echoState = speex_echo_state_init(FRAME_SIZE, FILTER_LENGTH);
        if (!echoState) {
            std::cerr << "Failed to initialize Speex echo state" << std::endl;
            return false;
        }

        // Set sampling rate
        int sampleRate = TARGET_SAMPLE_RATE;
        speex_echo_ctl(echoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);

        // Initialize preprocessor
        preprocessState = speex_preprocess_state_init(FRAME_SIZE, TARGET_SAMPLE_RATE);
        if (!preprocessState) {
            std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
            return false;
        }

        // Associate echo cancellation and preprocessor - based on Mumble's design
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState);

        // Configure preprocessor - based on Mumble's settings
        int denoise = 1;
        int agc = 1;
        int vad = 0;  // Disable VAD to avoid warnings
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

        return true;
    }

    bool setupAudioStreams() {
        // Setup microphone input stream
        PaStreamParameters micParams;
        micParams.device = Pa_GetDefaultInputDevice();
        if (micParams.device == paNoDevice) {
            std::cerr << "No default input device found" << std::endl;
            return false;
        }

        micParams.channelCount = CHANNELS;
        micParams.sampleFormat = paInt16;
        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
        micParams.hostApiSpecificStreamInfo = nullptr;

        // Setup output stream
        PaStreamParameters outputParams;
        outputParams.device = Pa_GetDefaultOutputDevice();
        if (outputParams.device == paNoDevice) {
            std::cerr << "No default output device found" << std::endl;
            return false;
        }

        outputParams.channelCount = CHANNELS;
        outputParams.sampleFormat = paInt16;
        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = nullptr;

        // Open microphone stream
        PaError err = Pa_OpenStream(&micStream,
            &micParams,
            nullptr,
            TARGET_SAMPLE_RATE,
            FRAME_SIZE,
            paClipOff,
            micCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open mic stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // Open output stream
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
    // Microphone callback - using Mumble's AddMic approach
    static int micCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        BasicEchoCancellation* ec = static_cast<BasicEchoCancellation*>(userData);
        const int16_t* input = static_cast<const int16_t*>(inputBuffer);

        if (input) {
            std::vector<int16_t> frame(input, input + framesPerBuffer);
            
            // Use Mumble's AddMic approach
            ec->addMic(frame);
        }

        return paContinue;
    }

    // Output callback
    static int outputCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        BasicEchoCancellation* ec = static_cast<BasicEchoCancellation*>(userData);
        int16_t* output = static_cast<int16_t*>(outputBuffer);

        std::unique_lock<std::mutex> lock(ec->outputMutex);

        if (!ec->outputBuffer.empty()) {
            std::vector<int16_t> frame = ec->outputBuffer.front();
            ec->outputBuffer.pop();
            lock.unlock();

            std::copy(frame.begin(), frame.end(), output);
        } else {
            // Output silence
            std::fill(output, output + framesPerBuffer, 0);
        }

        return paContinue;
    }

    // Mumble's AddMic state machine approach
    void addMic(const std::vector<int16_t>& micData) {
        bool drop = false;
        
        {
            std::lock_guard<std::mutex> lock(syncMutex);
            micQueue.push(micData);
            
            // Mumble's simple state machine: 0->1->2->3->4->5 (drop)
            if (state < 5) {
                state++;
            } else {
                drop = true;
                micQueue.pop(); // Remove the frame we just added
            }
        }
        
        if (drop) {
            droppedFrames++;
        }
        
        syncCondition.notify_one();
    }

    // Mumble's AddSpeaker state machine approach
    std::pair<std::vector<int16_t>, std::vector<int16_t>> addSpeaker(const std::vector<int16_t>& speakerData) {
        std::vector<int16_t> micData;
        bool drop = false;
        
        {
            std::lock_guard<std::mutex> lock(syncMutex);
            
            // Mumble's simple state machine: 5->4->3->2->1->0 (drop)
            if (state > 0) {
                state--;
                if (!micQueue.empty()) {
                    micData = micQueue.front();
                    micQueue.pop();
                }
            } else {
                drop = true;
            }
        }
        
        if (drop) {
            droppedFrames++;
            return {{}, {}}; // Return empty pair
        }
        
        return {micData, speakerData};
    }

    // Audio processing using Mumble's state machine
    void processAudio() {
        std::vector<int16_t> micFrame(FRAME_SIZE);
        std::vector<int16_t> outputFrame(FRAME_SIZE);
        
        // Create dummy speaker data for testing (silence)
        std::vector<int16_t> dummySpeakerData(FRAME_SIZE, 0);

        std::cout << "Audio processing started with Mumble state machine" << std::endl;
        std::cout << "Note: Using dummy speaker data for echo cancellation testing" << std::endl;

        while (running) {
            // Wait for microphone data
            std::unique_lock<std::mutex> lock(syncMutex);
            syncCondition.wait(lock, [this] { return !micQueue.empty() || !running; });

            if (!running) break;

            if (!micQueue.empty()) {
                // Get synchronized pair using Mumble's approach
                std::pair<std::vector<int16_t>, std::vector<int16_t>> pair = addSpeaker(dummySpeakerData);
                std::vector<int16_t> micData = pair.first;
                std::vector<int16_t> speakerData = pair.second;
                
                if (!micData.empty() && !speakerData.empty()) {
                    // Perform echo cancellation
                    speex_echo_cancellation(echoState,
                        speakerData.data(),
                        micData.data(),
                        outputFrame.data());

                    // Apply preprocessing - based on Mumble's speex_preprocess_run
                    speex_preprocess_run(preprocessState, outputFrame.data());

                    // Add to output buffer
                    std::lock_guard<std::mutex> outputLock(outputMutex);
                    outputBuffer.push(outputFrame);

                    // Limit output buffer size
                    while (outputBuffer.size() > MAX_BUFFER_SIZE) {
                        outputBuffer.pop();
                    }

                    processedFrames++;
                    
                    // Log every 100 frames
                    if (processedFrames % 100 == 0) {
                        std::cout << "Processed " << processedFrames << " frames - "
                                  << "Dropped: " << droppedFrames << " - "
                                  << "Queue: " << micQueue.size() << " - "
                                  << "State: " << state << std::endl;
                    }
                }
            }
        }
    }

    bool start() {
        if (running) return false;

        std::cout << "Starting basic echo cancellation with Mumble state machine..." << std::endl;

        running = true;

        // Start processing thread
        processingThread = std::thread(&BasicEchoCancellation::processAudio, this);

        // Start microphone stream
        PaError err = Pa_StartStream(micStream);
        if (err != paNoError) {
            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            return false;
        }

        // Start output stream
        err = Pa_StartStream(outputStream);
        if (err != paNoError) {
            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            Pa_StopStream(micStream);
            return false;
        }

        std::cout << "Basic echo cancellation started successfully!" << std::endl;
        std::cout << "Using Mumble's state machine for synchronization" << std::endl;
        std::cout << "Note: Using dummy speaker data for testing" << std::endl;
        std::cout << "Press Enter to stop..." << std::endl;

        return true;
    }

    void stop() {
        if (!running) return;

        std::cout << "Stopping basic echo cancellation..." << std::endl;

        running = false;

        // Stop audio streams
        if (micStream) Pa_StopStream(micStream);
        if (outputStream) Pa_StopStream(outputStream);

        // Wake up processing thread
        syncCondition.notify_all();

        // Wait for processing thread to end
        if (processingThread.joinable()) {
            processingThread.join();
        }

        std::cout << "Basic echo cancellation stopped." << std::endl;
        std::cout << "Processed frames: " << processedFrames << std::endl;
        std::cout << "Dropped frames: " << droppedFrames << std::endl;
        std::cout << "Final state: " << state << std::endl;
    }

    void cleanup() {
        stop();

        // Close streams
        if (micStream) {
            Pa_CloseStream(micStream);
            micStream = nullptr;
        }
        if (outputStream) {
            Pa_CloseStream(outputStream);
            outputStream = nullptr;
        }

        // Cleanup Speex
        if (echoState) {
            speex_echo_state_destroy(echoState);
            echoState = nullptr;
        }
        if (preprocessState) {
            speex_preprocess_state_destroy(preprocessState);
            preprocessState = nullptr;
        }

        // Terminate PortAudio
        Pa_Terminate();
    }
};

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Basic Real-Time Echo Cancellation Demo" << std::endl;
    std::cout << "Using Mumble's State Machine for Synchronization" << std::endl;
    std::cout << "=========================================" << std::endl;

    BasicEchoCancellation ec;

    if (!ec.initialize()) {
        std::cerr << "Failed to initialize echo cancellation" << std::endl;
        return -1;
    }

    if (!ec.start()) {
        std::cerr << "Failed to start echo cancellation" << std::endl;
        return -1;
    }

    // Wait for user input to stop
    std::cin.get();

    ec.stop();

    return 0;
} 