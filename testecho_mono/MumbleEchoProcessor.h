#ifndef MUMBLE_ECHO_PROCESSOR_H
#define MUMBLE_ECHO_PROCESSOR_H

#include "MumbleEchoTypes.h"
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <speex/speex_resampler.h>
#include <vector>

// Mumble's exact echo cancellation processor
class MumbleEchoProcessor {
public:
    MumbleEchoProcessor();
    ~MumbleEchoProcessor();

    // Initialize the processor with Mumble's exact settings
    bool initialize();

    // Process microphone data (like Mumble's addMic)
    void addMic(const void *data, unsigned int nsamp);

    // Process speaker data (like Mumble's addEcho)
    void addEcho(const void *data, unsigned int nsamp);

    // Get processed audio output
    bool getProcessedAudio(short *output, unsigned int maxSamples);

    // Reset the processor
    void reset();

    // Enable/disable debug output
    void setDebugOutput(bool enable) { bDebugOutput = enable; }

private:
    // Initialize Speex components exactly like Mumble
    bool initializeSpeex();

    // Initialize audio mixer functions
    void initializeMixer();

    // Choose mixer function (like Mumble)
    inMixerFunc chooseMixer(const unsigned int nchan, SampleFormat sf, unsigned long long mask);

    // Process audio frame (like Mumble's encodeAudioFrame)
    void processAudioFrame(AudioChunk chunk);

    // Audio parameters (exactly like Mumble)
    static const unsigned int iSampleRate = SAMPLE_RATE;
    static const int iFrameSize = FRAME_SIZE;

    // Audio format and channel info
    SampleFormat eMicFormat, eEchoFormat;
    unsigned int iMicChannels, iEchoChannels;
    unsigned int iMicFreq, iEchoFreq;
    unsigned int iMicLength, iEchoLength;
    unsigned int iMicSampleSize, iEchoSampleSize;
    unsigned int iEchoMCLength, iEchoFrameSize;
    unsigned long long uiMicChannelMask, uiEchoChannelMask;

    // Echo cancellation settings
    bool bEchoMulti;
    bool bResetProcessor;

    // Speex components (exactly like Mumble)
    SpeexEchoState *sesEcho;
    SpeexPreprocessState *preprocessState;
    SpeexResamplerState *srsMic, *srsEcho;

    // Audio buffers (like Mumble)
    float *pfMicInput;
    float *pfEchoInput;

    // Mixer functions (like Mumble)
    inMixerFunc imfMic, imfEcho;

    // Resynchronizer (exactly like Mumble)
    Resynchronizer resync;

    // Buffer management
    unsigned int iMicFilled, iEchoFilled;

    // Output buffer
    std::vector<short> outputBuffer;
    std::mutex outputMutex;

    // Statistics
    unsigned int processedFrames;
    unsigned int droppedFrames;

    // Debug
    bool bDebugOutput;
};

#endif // MUMBLE_ECHO_PROCESSOR_H 