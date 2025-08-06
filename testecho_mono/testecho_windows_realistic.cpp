#include "MumbleEchoProcessor.h"
#include <portaudio.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

// Define M_PI if not defined
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <initguid.h>
#include <audiodevice.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "propsys.lib")
#endif

// Windows-specific realistic echo cancellation test
class WindowsRealisticEchoTest {
private:
    // PortAudio streams
    PaStream* micStream;
    PaStream* outputStream;

    // Mumble's exact echo processor
    MumbleEchoProcessor* echoProcessor;

    // Audio buffers
    std::vector<short> micBuffer;
    std::vector<short> outputBuffer;

    // Control
    std::atomic<bool> running;

    // Statistics
    std::atomic<unsigned int> processedFrames;
    std::atomic<unsigned int> droppedFrames;
    std::atomic<unsigned int> micFrames;
    std::atomic<unsigned int> speakerFrames;

    // Test tone generator
    std::atomic<double> testTonePhase;
    std::atomic<bool> generateTestTone;

#ifdef _WIN32
    // Windows WASAPI for speaker capture
    IMMDeviceEnumerator* deviceEnumerator;
    IMMDevice* defaultDevice;
    IAudioClient* audioClient;
    IAudioCaptureClient* captureClient;
    std::thread speakerCaptureThread;
    std::atomic<bool> speakerCaptureRunning;
#endif

public:
    WindowsRealisticEchoTest() : 
        micStream(nullptr), 
        outputStream(nullptr),
        echoProcessor(nullptr),
        running(false), 
        processedFrames(0), 
        droppedFrames(0),
        micFrames(0),
        speakerFrames(0),
        testTonePhase(0.0),
        generateTestTone(true)
#ifdef _WIN32
        , deviceEnumerator(nullptr),
        defaultDevice(nullptr),
        audioClient(nullptr),
        captureClient(nullptr),
        speakerCaptureRunning(false)
#endif
    {
        micBuffer.resize(FRAME_SIZE);
        outputBuffer.resize(FRAME_SIZE);
    }

    ~WindowsRealisticEchoTest() {
        cleanup();
    }

    bool initialize() {
        std::cout << "Initializing Windows Realistic Echo Test..." << std::endl;

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

#ifdef _WIN32
        // Setup Windows WASAPI speaker capture
        if (!setupWASAPISpeakerCapture()) {
            std::cout << "WASAPI speaker capture failed, using test tone instead" << std::endl;
        }
#endif

        std::cout << "Windows Realistic Echo Test initialized successfully!" << std::endl;
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

        // Setup output stream for processed audio
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

        // Open output stream for processed audio
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

#ifdef _WIN32
    bool setupWASAPISpeakerCapture() {
        // Initialize COM
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize COM" << std::endl;
            return false;
        }

        // Create device enumerator
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
        if (FAILED(hr)) {
            std::cerr << "Failed to create device enumerator" << std::endl;
            return false;
        }

        // Get default audio render device
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
        if (FAILED(hr)) {
            std::cerr << "Failed to get default audio device" << std::endl;
            return false;
        }

        // Create audio client
        hr = defaultDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
        if (FAILED(hr)) {
            std::cerr << "Failed to create audio client" << std::endl;
            return false;
        }

        // Initialize audio client for loopback capture
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;  // Mono
        wfx.nSamplesPerSec = SAMPLE_RATE;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            10000000,  // 1 second buffer
            0,
            &wfx,
            nullptr);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize audio client for loopback" << std::endl;
            return false;
        }

        // Get capture client
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr)) {
            std::cerr << "Failed to get capture client" << std::endl;
            return false;
        }

        std::cout << "WASAPI speaker capture setup successful!" << std::endl;
        return true;
    }
#endif

public:
    // Microphone callback
    static int micCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        WindowsRealisticEchoTest* test = static_cast<WindowsRealisticEchoTest*>(userData);
        const short* input = static_cast<const short*>(inputBuffer);

        if (input && test->echoProcessor) {
            // Use Mumble's exact addMic method
            test->echoProcessor->addMic(input, framesPerBuffer);
            test->micFrames++;
        }

        return paContinue;
    }

    // Output callback for processed audio
    static int outputCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {

        WindowsRealisticEchoTest* test = static_cast<WindowsRealisticEchoTest*>(userData);
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

#ifdef _WIN32
    void speakerCaptureThreadFunc() {
        if (!audioClient || !captureClient) return;

        // Start capture
        HRESULT hr = audioClient->Start();
        if (FAILED(hr)) {
            std::cerr << "Failed to start audio capture" << std::endl;
            return;
        }

        std::cout << "Speaker capture started!" << std::endl;

        while (speakerCaptureRunning) {
            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;

            if (packetLength > 0) {
                BYTE* data;
                UINT32 framesAvailable;
                DWORD flags;
                UINT64 devicePosition;
                UINT64 qpcPosition;

                hr = captureClient->GetBuffer(&data, &framesAvailable, &flags, &devicePosition, &qpcPosition);
                if (SUCCEEDED(hr)) {
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                        // Convert to 16-bit PCM and send to echo processor
                        short* audioData = reinterpret_cast<short*>(data);
                        if (echoProcessor) {
                            echoProcessor->addEcho(audioData, framesAvailable);
                            speakerFrames++;
                        }
                    }
                    captureClient->ReleaseBuffer(framesAvailable);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        audioClient->Stop();
    }
#endif

    bool start() {
        std::cout << "Starting Windows realistic echo test..." << std::endl;

        running = true;

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

#ifdef _WIN32
        // Start WASAPI speaker capture if available
        if (audioClient && captureClient) {
            speakerCaptureRunning = true;
            speakerCaptureThread = std::thread(&WindowsRealisticEchoTest::speakerCaptureThreadFunc, this);
            std::cout << "WASAPI speaker capture started!" << std::endl;
        } else {
            std::cout << "No WASAPI capture available - using test tone" << std::endl;
            // Start test tone generation thread
            std::thread([this]() {
                while (running) {
                    if (generateTestTone && echoProcessor) {
                        std::vector<short> testTone(FRAME_SIZE);
                        const double frequency = 1000.0; // 1kHz
                        const double amplitude = 0.3;
                        
                        for (int i = 0; i < FRAME_SIZE; ++i) {
                            double phase = testTonePhase.load();
                            testTone[i] = static_cast<short>(amplitude * 32767.0 * sin(2.0 * M_PI * frequency * phase / SAMPLE_RATE));
                            phase += 1.0;
                            if (phase >= SAMPLE_RATE) phase -= SAMPLE_RATE;
                            testTonePhase.store(phase);
                        }
                        
                        // Send test tone to echo processor for cancellation (but don't play it)
                        echoProcessor->addEcho(testTone.data(), FRAME_SIZE);
                        speakerFrames++;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }).detach();
        }
#else
        // Non-Windows: use test tone
        std::cout << "Non-Windows system - using test tone" << std::endl;
        std::thread([this]() {
            while (running) {
                if (generateTestTone && echoProcessor) {
                    std::vector<short> testTone(FRAME_SIZE);
                    const double frequency = 1000.0; // 1kHz
                    const double amplitude = 0.3;
                    
                    for (int i = 0; i < FRAME_SIZE; ++i) {
                        double phase = testTonePhase.load();
                        testTone[i] = static_cast<short>(amplitude * 32767.0 * sin(2.0 * M_PI * frequency * phase / SAMPLE_RATE));
                        phase += 1.0;
                        if (phase >= SAMPLE_RATE) phase -= SAMPLE_RATE;
                        testTonePhase.store(phase);
                    }
                    
                    // Send test tone to echo processor for cancellation (but don't play it)
                    echoProcessor->addEcho(testTone.data(), FRAME_SIZE);
                    speakerFrames++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }).detach();
#endif

        std::cout << "Windows realistic echo test started successfully!" << std::endl;
        std::cout << "You should hear:" << std::endl;
        std::cout << "1. Your microphone input with echo cancellation applied" << std::endl;
        std::cout << "2. The echo cancellation should reduce any test tone echo in your mic input" << std::endl;
        std::cout << "Press Enter to stop..." << std::endl;

        return true;
    }

    void stop() {
        std::cout << "Stopping Windows realistic echo test..." << std::endl;

        running = false;

#ifdef _WIN32
        if (speakerCaptureRunning) {
            speakerCaptureRunning = false;
            if (speakerCaptureThread.joinable()) {
                speakerCaptureThread.join();
            }
        }
#endif

        if (micStream) Pa_StopStream(micStream);
        if (outputStream) Pa_StopStream(outputStream);

        std::cout << "Windows realistic echo test stopped." << std::endl;
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Mic frames: " << micFrames << std::endl;
        std::cout << "  Speaker frames: " << speakerFrames << std::endl;
        std::cout << "  Processed frames: " << processedFrames << std::endl;
        std::cout << "  Dropped frames: " << droppedFrames << std::endl;
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

        // Cleanup echo processor
        if (echoProcessor) {
            delete echoProcessor;
            echoProcessor = nullptr;
        }

#ifdef _WIN32
        // Cleanup WASAPI
        if (captureClient) {
            captureClient->Release();
            captureClient = nullptr;
        }
        if (audioClient) {
            audioClient->Release();
            audioClient = nullptr;
        }
        if (defaultDevice) {
            defaultDevice->Release();
            defaultDevice = nullptr;
        }
        if (deviceEnumerator) {
            deviceEnumerator->Release();
            deviceEnumerator = nullptr;
        }
        CoUninitialize();
#endif

        // Terminate PortAudio
        Pa_Terminate();
    }

    // Control test tone
    void setTestTone(bool enable) {
        generateTestTone = enable;
        if (enable) {
            std::cout << "Test tone enabled (1kHz sine wave)" << std::endl;
        } else {
            std::cout << "Test tone disabled" << std::endl;
        }
    }
};

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Windows Realistic Echo Cancellation Test" << std::endl;
    std::cout << "Using WASAPI Loopback for Speaker Capture" << std::endl;
    std::cout << "=========================================" << std::endl;

    WindowsRealisticEchoTest test;

    if (!test.initialize()) {
        std::cerr << "Failed to initialize Windows realistic echo test" << std::endl;
        return -1;
    }

    if (!test.start()) {
        std::cerr << "Failed to start Windows realistic echo test" << std::endl;
        return -1;
    }

    // Interactive control
    std::cout << "\nControls:" << std::endl;
    std::cout << "  't' + Enter: Enable test tone" << std::endl;
    std::cout << "  'f' + Enter: Disable test tone" << std::endl;
    std::cout << "  Enter: Stop and exit" << std::endl;

    std::string input;
    while (std::getline(std::cin, input)) {
        if (input.empty()) {
            break; // Exit
        } else if (input == "t") {
            test.setTestTone(true);
        } else if (input == "f") {
            test.setTestTone(false);
        }
    }

    test.stop();
    return 0;
} 