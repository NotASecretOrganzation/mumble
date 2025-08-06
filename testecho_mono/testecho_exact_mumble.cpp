//#include "MumbleEchoProcessor.h"
//#include <portaudio.h>
//#include <iostream>
//#include <vector>
//#include <thread>
//#include <atomic>
//
//// Test program using Mumble's exact echo cancellation
//class ExactMumbleEchoTest {
//private:
//    // PortAudio streams
//    PaStream* micStream;
//    PaStream* outputStream;
//
//    // Mumble's exact echo processor
//    MumbleEchoProcessor* echoProcessor;
//
//    // Audio buffers
//    std::vector<short> micBuffer;
//    std::vector<short> outputBuffer;
//
//    // Control
//    std::atomic<bool> running;
//
//    // Statistics
//    std::atomic<unsigned int> processedFrames;
//    std::atomic<unsigned int> droppedFrames;
//
//public:
//    ExactMumbleEchoTest() : 
//        micStream(nullptr), 
//        outputStream(nullptr),
//        echoProcessor(nullptr),
//        running(false),
//        processedFrames(0),
//        droppedFrames(0) {
//        
//        micBuffer.resize(FRAME_SIZE);
//        outputBuffer.resize(FRAME_SIZE);
//    }
//
//    ~ExactMumbleEchoTest() {
//        cleanup();
//    }
//
//    bool initialize() {
//        std::cout << "Initializing Exact Mumble Echo Test..." << std::endl;
//
//        // Initialize PortAudio
//        PaError err = Pa_Initialize();
//        if (err != paNoError) {
//            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
//            return false;
//        }
//
//        // Create Mumble's exact echo processor
//        echoProcessor = new MumbleEchoProcessor();
//        echoProcessor->setDebugOutput(true);
//
//        if (!echoProcessor->initialize()) {
//            std::cerr << "Failed to initialize Mumble echo processor" << std::endl;
//            return false;
//        }
//
//        // Setup audio streams
//        if (!setupAudioStreams()) {
//            return false;
//        }
//
//        std::cout << "Exact Mumble Echo Test initialized successfully!" << std::endl;
//        return true;
//    }
//
//private:
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
//        ExactMumbleEchoTest* test = static_cast<ExactMumbleEchoTest*>(userData);
//        const short* input = static_cast<const short*>(inputBuffer);
//
//        if (input && test->echoProcessor) {
//            // Use Mumble's exact addMic method
//            test->echoProcessor->addMic(input, framesPerBuffer);
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
//        ExactMumbleEchoTest* test = static_cast<ExactMumbleEchoTest*>(userData);
//        short* output = static_cast<short*>(outputBuffer);
//
//        if (test->echoProcessor) {
//            // Get processed audio from Mumble's processor
//            if (test->echoProcessor->getProcessedAudio(output, framesPerBuffer)) {
//                test->processedFrames++;
//            } else {
//                // Output silence if no processed audio available
//                std::fill(output, output + framesPerBuffer, 0);
//            }
//        } else {
//            // Output silence
//            std::fill(output, output + framesPerBuffer, 0);
//        }
//
//        return paContinue;
//    }
//
//    // Add dummy speaker data for testing
//    void addDummySpeakerData() {
//        if (echoProcessor) {
//            // Create dummy speaker data (silence for testing)
//            std::vector<short> dummySpeaker(FRAME_SIZE, 0);
//            echoProcessor->addEcho(dummySpeaker.data(), FRAME_SIZE);
//        }
//    }
//
//    bool start() {
//        std::cout << "Starting exact Mumble echo test..." << std::endl;
//
//        running = true;
//
//        // Start microphone stream
//        PaError err = Pa_StartStream(micStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            return false;
//        }
//
//        // Start output stream
//        err = Pa_StartStream(outputStream);
//        if (err != paNoError) {
//            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
//            running = false;
//            Pa_StopStream(micStream);
//            return false;
//        }
//
//        std::cout << "Exact Mumble echo test started successfully!" << std::endl;
//        std::cout << "Note: Using dummy speaker data (silence) for testing" << std::endl;
//        std::cout << "Press Enter to stop..." << std::endl;
//
//        return true;
//    }
//
//    void stop() {
//        std::cout << "Stopping exact Mumble echo test..." << std::endl;
//
//        running = false;
//
//        if (micStream) Pa_StopStream(micStream);
//        if (outputStream) Pa_StopStream(outputStream);
//
//        std::cout << "Exact Mumble echo test stopped." << std::endl;
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
//        // Cleanup echo processor
//        if (echoProcessor) {
//            delete echoProcessor;
//            echoProcessor = nullptr;
//        }
//
//        // Terminate PortAudio
//        Pa_Terminate();
//    }
//};
//
//int main() {
//    std::cout << "=========================================" << std::endl;
//    std::cout << "Exact Mumble Echo Cancellation Test" << std::endl;
//    std::cout << "Using Mumble's Exact Implementation" << std::endl;
//    std::cout << "=========================================" << std::endl;
//
//    ExactMumbleEchoTest test;
//
//    if (!test.initialize()) {
//        std::cerr << "Failed to initialize exact Mumble echo test" << std::endl;
//        return -1;
//    }
//
//    if (!test.start()) {
//        std::cerr << "Failed to start exact Mumble echo test" << std::endl;
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