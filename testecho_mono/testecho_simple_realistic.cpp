#include "MumbleEchoProcessor.h"
#include <portaudio.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>

// Define M_PI if not defined
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Simple realistic echo cancellation test
class SimpleRealisticEchoTest {
private:
    // PortAudio streams
    PaStream* micStream;
    PaStream* testToneStream;  // Stream to generate test tone
    PaStream* outputStream;    // Stream to play processed audio

    // Mumble's exact echo processor
    MumbleEchoProcessor* echoProcessor;

    // Control
    std::atomic<bool> running;

    // Statistics
    std::atomic<unsigned int> processedFrames;
    std::atomic<unsigned int> micFrames;
    std::atomic<unsigned int> testToneFrames;

    // Test tone generator
    std::atomic<double> testTonePhase;
    std::atomic<bool> generateTestTone;
    std::atomic<double> testToneFrequency;
    std::atomic<double> testToneAmplitude;

public:
    SimpleRealisticEchoTest() : 
        micStream(nullptr), 
        testToneStream(nullptr),
        outputStream(nullptr),
        echoProcessor(nullptr),
        running(false), 
        processedFrames(0), 
        micFrames(0),
        testToneFrames(0),
        testTonePhase(0.0),
        generateTestTone(true),
        testToneFrequency(1000.0),  // 1kHz default
        testToneAmplitude(0.5)      // 50% volume
    {
    }

    ~SimpleRealisticEchoTest() {
        cleanup();
    }

    bool initialize() {
        std::cout << "Initializing Simple Realistic Echo Test..." << std::endl;

        // Initialize PortAudio
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // Create Mumble's exact echo processor
        echoProcessor = new MumbleEchoProcessor();
        echoProcessor->setDebugOutput(true);

        if (!echoProcessor->initialize()) {
            std::cerr << "Failed to initialize Mumble echo processor" << std::endl;
            return false;
        }

        // Setup audio streams
        if (!setupAudioStreams()) {
            return false;
        }

        std::cout << "Simple Realistic Echo Test initialized successfully!" << std::endl;
        std::cout << "This will generate a test tone and use it for echo cancellation" << std::endl;
        return true;
    }

private:
    bool setupAudioStreams() {
        // Setup microphone input stream
        PaStreamParameters micParams;
        micParams.device = Pa_GetDefaultInputDevice();
        if (micParams.device == paNoDevice) {
            std::cerr << "No default input device found" << std::endl;
            return false;
        }

        micParams.channelCount = 1;  // Mono like Mumble
        micParams.sampleFormat = paInt16;
        micParams.suggestedLatency = Pa_GetDeviceInfo(micParams.device)->defaultLowInputLatency;
        micParams.hostApiSpecificStreamInfo = nullptr;

        // Setup test tone output stream
        PaStreamParameters testToneParams;
        testToneParams.device = Pa_GetDefaultOutputDevice();
        if (testToneParams.device == paNoDevice) {
            std::cerr << "No default output device found" << std::endl;
            return false;
        }

        testToneParams.channelCount = 1;  // Mono like Mumble
        testToneParams.sampleFormat = paInt16;
        testToneParams.suggestedLatency = Pa_GetDeviceInfo(testToneParams.device)->defaultLowOutputLatency;
        testToneParams.hostApiSpecificStreamInfo = nullptr;

        // Setup processed audio output stream
        PaStreamParameters outputParams;
        outputParams.device = Pa_GetDefaultOutputDevice();
        if (outputParams.device == paNoDevice) {
            std::cerr << "No default output device found" << std::endl;
            return false;
        }

        outputParams.channelCount = 1;  // Mono like Mumble
        outputParams.sampleFormat = paInt16;
        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = nullptr;

        // Open microphone stream
        PaError err = Pa_OpenStream(&micStream,
            &micParams,
            nullptr,
            SAMPLE_RATE,
            FRAME_SIZE,
            paClipOff,
            micCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open mic stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // Open test tone stream
        err = Pa_OpenStream(&testToneStream,
            nullptr,
            &testToneParams,
            SAMPLE_RATE,
            FRAME_SIZE,
            paClipOff,
            testToneCallback,
            this);

        if (err != paNoError) {
            std::cerr << "Failed to open test tone stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        // Open processed audio output stream
        err = Pa_OpenStream(&outputStream,
            nullptr,
            &outputParams,
            SAMPLE_RATE,
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
    // Microphone callback
    static int micCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        SimpleRealisticEchoTest* test = static_cast<SimpleRealisticEchoTest*>(userData);
        const short* input = static_cast<const short*>(inputBuffer);

        if (input && test->echoProcessor) {
            // Use Mumble's exact addMic method
            test->echoProcessor->addMic(input, framesPerBuffer);
            test->micFrames++;
        }

        return paContinue;
    }

    // Test tone callback
    static int testToneCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        SimpleRealisticEchoTest* test = static_cast<SimpleRealisticEchoTest*>(userData);
        short* output = static_cast<short*>(outputBuffer);

        // Generate test tone
        if (test->generateTestTone) {
            double frequency = test->testToneFrequency.load();
            double amplitude = test->testToneAmplitude.load();
            
            // Generate test tone data for echo cancellation (but don't play it)
            std::vector<short> testToneData(framesPerBuffer);
            for (unsigned long i = 0; i < framesPerBuffer; ++i) {
                double phase = test->testTonePhase.load();
                testToneData[i] = static_cast<short>(amplitude * 32767.0 * sin(2.0 * M_PI * frequency * phase / SAMPLE_RATE));
                phase += 1.0;
                if (phase >= SAMPLE_RATE) phase -= SAMPLE_RATE;
                test->testTonePhase.store(phase);
            }

            // Send the test tone to echo processor for cancellation
            if (test->echoProcessor) {
                test->echoProcessor->addEcho(testToneData.data(), framesPerBuffer);
                test->testToneFrames++;
            }

            // Output silence instead of test tone (commented out speaker output)
            std::fill(output, output + framesPerBuffer, 0);
        } else {
            // Output silence
            std::fill(output, output + framesPerBuffer, 0);
        }

        return paContinue;
    }

    // Output callback for processed audio
    static int outputCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        SimpleRealisticEchoTest* test = static_cast<SimpleRealisticEchoTest*>(userData);
        short* output = static_cast<short*>(outputBuffer);

        if (test->echoProcessor) {
            // Get processed audio from Mumble's processor
            if (test->echoProcessor->getProcessedAudio(output, framesPerBuffer)) {
                test->processedFrames++;
            } else {
                // Output silence if no processed audio available
                std::fill(output, output + framesPerBuffer, 0);
            }
        } else {
            // Output silence
            std::fill(output, output + framesPerBuffer, 0);
        }

        return paContinue;
    }

    bool start() {
        std::cout << "Starting simple realistic echo test..." << std::endl;

        running = true;

        // Start microphone stream
        PaError err = Pa_StartStream(micStream);
        if (err != paNoError) {
            std::cerr << "Failed to start mic stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            return false;
        }

        // Start test tone stream
        err = Pa_StartStream(testToneStream);
        if (err != paNoError) {
            std::cerr << "Failed to start test tone stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            Pa_StopStream(micStream);
            return false;
        }

        // Start output stream
        err = Pa_StartStream(outputStream);
        if (err != paNoError) {
            std::cerr << "Failed to start output stream: " << Pa_GetErrorText(err) << std::endl;
            running = false;
            Pa_StopStream(micStream);
            Pa_StopStream(testToneStream);
            return false;
        }

        std::cout << "Simple realistic echo test started successfully!" << std::endl;
        std::cout << "You should hear:" << std::endl;
        std::cout << "1. Your microphone input with echo cancellation applied" << std::endl;
        std::cout << "2. The echo cancellation should reduce any test tone echo in your mic input" << std::endl;
        std::cout << "Press Enter to stop..." << std::endl;

        return true;
    }

    void stop() {
        std::cout << "Stopping simple realistic echo test..." << std::endl;

        running = false;

        if (micStream) Pa_StopStream(micStream);
        if (testToneStream) Pa_StopStream(testToneStream);
        if (outputStream) Pa_StopStream(outputStream);

        std::cout << "Simple realistic echo test stopped." << std::endl;
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Mic frames: " << micFrames << std::endl;
        std::cout << "  Test tone frames: " << testToneFrames << std::endl;
        std::cout << "  Processed frames: " << processedFrames << std::endl;
    }

    void cleanup() {
        stop();

        // Close streams
        if (micStream) {
            Pa_CloseStream(micStream);
            micStream = nullptr;
        }
        if (testToneStream) {
            Pa_CloseStream(testToneStream);
            testToneStream = nullptr;
        }
        if (outputStream) {
            Pa_CloseStream(outputStream);
            outputStream = nullptr;
        }

        // Cleanup echo processor
        if (echoProcessor) {
            delete echoProcessor;
            echoProcessor = nullptr;
        }

        // Terminate PortAudio
        Pa_Terminate();
    }

    // Control test tone
    void setTestTone(bool enable) {
        generateTestTone = enable;
        if (enable) {
            std::cout << "Test tone enabled (" << testToneFrequency.load() << "Hz)" << std::endl;
        } else {
            std::cout << "Test tone disabled" << std::endl;
        }
    }

    void setTestToneFrequency(double frequency) {
        testToneFrequency = frequency;
        std::cout << "Test tone frequency set to " << frequency << "Hz" << std::endl;
    }

    void setTestToneAmplitude(double amplitude) {
        testToneAmplitude = amplitude;
        std::cout << "Test tone amplitude set to " << (amplitude * 100) << "%" << std::endl;
    }
};

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Simple Realistic Echo Cancellation Test" << std::endl;
    std::cout << "Using Test Tone for Echo Cancellation" << std::endl;
    std::cout << "=========================================" << std::endl;

    SimpleRealisticEchoTest test;

    if (!test.initialize()) {
        std::cerr << "Failed to initialize simple realistic echo test" << std::endl;
        return -1;
    }

    if (!test.start()) {
        std::cerr << "Failed to start simple realistic echo test" << std::endl;
        return -1;
    }

    // Interactive control
    std::cout << "\nControls:" << std::endl;
    std::cout << "  't' + Enter: Enable test tone" << std::endl;
    std::cout << "  'f' + Enter: Disable test tone" << std::endl;
    std::cout << "  '1' + Enter: Set frequency to 1kHz" << std::endl;
    std::cout << "  '2' + Enter: Set frequency to 2kHz" << std::endl;
    std::cout << "  '5' + Enter: Set frequency to 500Hz" << std::endl;
    std::cout << "  'h' + Enter: Set amplitude to high (80%)" << std::endl;
    std::cout << "  'l' + Enter: Set amplitude to low (20%)" << std::endl;
    std::cout << "  Enter: Stop and exit" << std::endl;

    std::string input;
    while (std::getline(std::cin, input)) {
        if (input.empty()) {
            break; // Exit
        } else if (input == "t") {
            test.setTestTone(true);
        } else if (input == "f") {
            test.setTestTone(false);
        } else if (input == "1") {
            test.setTestToneFrequency(1000.0);
        } else if (input == "2") {
            test.setTestToneFrequency(2000.0);
        } else if (input == "5") {
            test.setTestToneFrequency(500.0);
        } else if (input == "h") {
            test.setTestToneAmplitude(0.8);
        } else if (input == "l") {
            test.setTestToneAmplitude(0.2);
        }
    }

    test.stop();
    return 0;
} 