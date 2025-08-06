#include "MumbleEchoProcessor.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Mixer functions (exactly like Mumble) - fixed syntax
static void inMixerFloatMask(float* buffer, const void* ipt, unsigned int nsamp, unsigned int N, unsigned long long mask) {
    const float *input = reinterpret_cast<const float *>(ipt);
    for (unsigned int i = 0; i < nsamp; ++i) {
        float sum = 0.0f;
        for (unsigned int j = 0; j < N; ++j) {
            if (mask & (1ULL << j)) {
                sum += input[i * N + j];
            }
        }
        buffer[i] = sum;
    }
}

static void inMixerShortMask(float* buffer, const void* ipt, unsigned int nsamp, unsigned int N, unsigned long long mask) {
    const short *input = reinterpret_cast<const short *>(ipt);
    for (unsigned int i = 0; i < nsamp; ++i) {
        float sum = 0.0f;
        for (unsigned int j = 0; j < N; ++j) {
            if (mask & (1ULL << j)) {
                sum += static_cast<float>(input[i * N + j]) * (1.0f / 32768.f);
            }
        }
        buffer[i] = sum;
    }
}

MumbleEchoProcessor::MumbleEchoProcessor() :
    eMicFormat(SampleShort), eEchoFormat(SampleShort),
    iMicChannels(1), iEchoChannels(1),
    iMicFreq(SAMPLE_RATE), iEchoFreq(SAMPLE_RATE),
    iMicFilled(0), iEchoFilled(0),
    sesEcho(nullptr), preprocessState(nullptr),
    srsMic(nullptr), srsEcho(nullptr),
    pfMicInput(nullptr), pfEchoInput(nullptr),
    imfMic(nullptr), imfEcho(nullptr),
    bEchoMulti(false), bResetProcessor(true),
    processedFrames(0), droppedFrames(0),
    bDebugOutput(false) {
    
    uiMicChannelMask = 0xffffffffffffffffULL;
    uiEchoChannelMask = 0xffffffffffffffffULL;
}

MumbleEchoProcessor::~MumbleEchoProcessor() {
    reset();
    
    if (pfMicInput) delete[] pfMicInput;
    if (pfEchoInput) delete[] pfEchoInput;
    if (srsMic) speex_resampler_destroy(srsMic);
    if (srsEcho) speex_resampler_destroy(srsEcho);
}

bool MumbleEchoProcessor::initialize() {
    if (bDebugOutput) {
        std::cout << "Initializing Mumble Echo Processor..." << std::endl;
    }

    // Initialize Speex components
    if (!initializeSpeex()) {
        return false;
    }

    // Initialize mixer
    initializeMixer();

    if (bDebugOutput) {
        std::cout << "Mumble Echo Processor initialized successfully!" << std::endl;
        std::cout << "Sample Rate: " << iSampleRate << "Hz" << std::endl;
        std::cout << "Frame Size: " << iFrameSize << " samples" << std::endl;
        std::cout << "Mic Channels: " << iMicChannels << ", Echo Channels: " << iEchoChannels << std::endl;
    }

    return true;
}

bool MumbleEchoProcessor::initializeSpeex() {
    // Initialize preprocessor (like Mumble)
    preprocessState = speex_preprocess_state_init(iFrameSize, iSampleRate);
    if (!preprocessState) {
        std::cerr << "Failed to initialize Speex preprocess state" << std::endl;
        return false;
    }

    // Configure preprocessor (exactly like Mumble's settings)
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

    // Initialize echo cancellation (like Mumble)
    if (iEchoChannels > 0) {
        int filterSize = iFrameSize * (10 + resync.getNominalLag());
        sesEcho = speex_echo_state_init_mc(iFrameSize, filterSize, 1, bEchoMulti ? static_cast<int>(iEchoChannels) : 1);
        if (!sesEcho) {
            std::cerr << "Failed to initialize Speex echo state" << std::endl;
            return false;
        }
        
        int sampleRate = iSampleRate;
        speex_echo_ctl(sesEcho, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
        speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, sesEcho);

        if (bDebugOutput) {
            std::cout << "ECHO CANCELLER ACTIVE" << std::endl;
        }
    }

    return true;
}

void MumbleEchoProcessor::initializeMixer() {
    int err;

    if (srsMic)
        speex_resampler_destroy(srsMic);
    if (srsEcho)
        speex_resampler_destroy(srsEcho);
    delete[] pfMicInput;
    delete[] pfEchoInput;

    if (iMicFreq != iSampleRate)
        srsMic = speex_resampler_init(1, iMicFreq, iSampleRate, 3, &err);

    iMicLength = (iFrameSize * iMicFreq) / iSampleRate;
    pfMicInput = new float[iMicLength];

    if (iEchoChannels > 0) {
        bEchoMulti = false; // Simplified for now
        if (iEchoFreq != iSampleRate)
            srsEcho = speex_resampler_init(bEchoMulti ? iEchoChannels : 1, iEchoFreq, iSampleRate, 3, &err);
        iEchoLength = (iFrameSize * iEchoFreq) / iSampleRate;
        iEchoMCLength = bEchoMulti ? iEchoLength * iEchoChannels : iEchoLength;
        iEchoFrameSize = bEchoMulti ? iFrameSize * iEchoChannels : iFrameSize;
        pfEchoInput = new float[iEchoMCLength];
    } else {
        srsEcho = nullptr;
        pfEchoInput = nullptr;
    }

    imfMic = chooseMixer(iMicChannels, eMicFormat, uiMicChannelMask);
    imfEcho = chooseMixer(iEchoChannels, eEchoFormat, uiEchoChannelMask);

    iMicSampleSize = static_cast<unsigned int>(iMicChannels * ((eMicFormat == SampleFloat) ? sizeof(float) : sizeof(short)));
    iEchoSampleSize = static_cast<unsigned int>(iEchoChannels * ((eEchoFormat == SampleFloat) ? sizeof(float) : sizeof(short)));

    bResetProcessor = true;
}

inMixerFunc MumbleEchoProcessor::chooseMixer(const unsigned int nchan, SampleFormat sf, unsigned long long chanmask) {
    if (nchan == 1) {
        if (sf == SampleFloat)
            return inMixerFloatMask;
        else
            return inMixerShortMask;
    } else {
        if (sf == SampleFloat)
            return inMixerFloatMask;
        else
            return inMixerShortMask;
    }
}

void MumbleEchoProcessor::addMic(const void *data, unsigned int nsamp) {
    while (nsamp > 0) {
        // Make sure we don't overrun the frame buffer
        const unsigned int left = std::min(nsamp, iMicLength - iMicFilled);

        // Append mix into pfMicInput frame buffer (converts 16bit pcm->float if necessary)
        imfMic(pfMicInput + iMicFilled, data, left, iMicChannels, uiMicChannelMask);

        iMicFilled += left;
        nsamp -= left;

        // If new samples are left offset data pointer to point at the first one for next iteration
        if (nsamp > 0) {
            if (eMicFormat == SampleFloat)
                data = reinterpret_cast<const float *>(data) + left * iMicChannels;
            else
                data = reinterpret_cast<const short *>(data) + left * iMicChannels;
        }

        if (iMicFilled == iMicLength) {
            // Frame complete
            iMicFilled = 0;

            // If needed resample frame
            float *pfOutput = srsMic ? (float *)alloca(iFrameSize * sizeof(float)) : nullptr;
            float *ptr = srsMic ? pfOutput : pfMicInput;

            if (srsMic) {
                spx_uint32_t inlen = iMicLength;
                spx_uint32_t outlen = iFrameSize;
                speex_resampler_process_float(srsMic, 0, pfMicInput, &inlen, pfOutput, &outlen);
            }

            // If echo cancellation is enabled the pointer ends up in the resynchronizer queue
            // and may need to outlive this function's frame
            short *psMic = iEchoChannels > 0 ? new short[iFrameSize] : (short *)alloca(iFrameSize * sizeof(short));

            // Convert float to 16bit PCM
            const float mul = 32768.f;
            for (int j = 0; j < iFrameSize; ++j)
                psMic[j] = static_cast<short>(std::max(-32768.f, std::min(32767.f, (ptr[j] * mul))));

            // If we have echo cancellation enabled...
            if (iEchoChannels > 0) {
                resync.addMic(psMic);
            } else {
                processAudioFrame(AudioChunk(psMic));
            }
        }
    }
}

void MumbleEchoProcessor::addEcho(const void *data, unsigned int nsamp) {
    while (nsamp > 0) {
        // Make sure we don't overrun the echo frame buffer
        const unsigned int left = std::min(nsamp, iEchoLength - iEchoFilled);

        if (bEchoMulti) {
            const unsigned int samples = left * iEchoChannels;

            if (eEchoFormat == SampleFloat) {
                for (unsigned int i = 0; i < samples; ++i)
                    pfEchoInput[i + iEchoFilled * iEchoChannels] = reinterpret_cast<const float *>(data)[i];
            } else {
                // 16bit PCM -> float
                for (unsigned int i = 0; i < samples; ++i)
                    pfEchoInput[i + iEchoFilled * iEchoChannels] =
                        static_cast<float>(reinterpret_cast<const short *>(data)[i]) * (1.0f / 32768.f);
            }
        } else {
            // Mix echo channels (converts 16bit PCM -> float if needed)
            imfEcho(pfEchoInput + iEchoFilled, data, left, iEchoChannels, uiEchoChannelMask);
        }

        iEchoFilled += left;
        nsamp -= left;

        // If new samples are left offset data pointer to point at the first one for next iteration
        if (nsamp > 0) {
            if (eEchoFormat == SampleFloat)
                data = reinterpret_cast<const float *>(data) + left * iEchoChannels;
            else
                data = reinterpret_cast<const short *>(data) + left * iEchoChannels;
        }

        if (iEchoFilled == iEchoLength) {
            // Frame complete
            iEchoFilled = 0;

            // Resample if necessary
            float *pfOutput = srsEcho ? (float *)alloca(iEchoFrameSize * sizeof(float)) : nullptr;
            float *ptr = srsEcho ? pfOutput : pfEchoInput;

            if (srsEcho) {
                spx_uint32_t inlen = iEchoLength;
                spx_uint32_t outlen = iFrameSize;
                speex_resampler_process_interleaved_float(srsEcho, pfEchoInput, &inlen, pfOutput, &outlen);
            }

            short *outbuff = new short[iEchoFrameSize];

            // float -> 16bit PCM
            const float mul = 32768.f;
            for (unsigned int j = 0; j < iEchoFrameSize; ++j) {
                outbuff[j] = static_cast<short>(std::max(-32768.f, std::min(32767.f, (ptr[j] * mul))));
            }

            auto chunk = resync.addSpeaker(outbuff);
            if (!chunk.empty()) {
                processAudioFrame(chunk);
                delete[] chunk.mic;
                delete[] chunk.speaker;
            }
        }
    }
}

void MumbleEchoProcessor::processAudioFrame(AudioChunk chunk) {
    // MUMBLE'S EXACT ECHO CANCELLATION CODE:
    short psClean[iFrameSize];
    short *psSource;

    if (sesEcho && chunk.speaker) {
        speex_echo_cancellation(sesEcho, chunk.mic, chunk.speaker, psClean);
        psSource = psClean;
    } else {
        psSource = chunk.mic;
    }

    // Apply preprocessing (exactly like Mumble)
    speex_preprocess_run(preprocessState, psSource);

    // Add to output buffer
    std::lock_guard<std::mutex> lock(outputMutex);
    outputBuffer.insert(outputBuffer.end(), psSource, psSource + iFrameSize);

    processedFrames++;
    
    if (bDebugOutput && processedFrames % 100 == 0) {
        std::cout << "Processed " << processedFrames << " frames with Mumble's echo cancellation" << std::endl;
    }
}

bool MumbleEchoProcessor::getProcessedAudio(short *output, unsigned int maxSamples) {
    std::lock_guard<std::mutex> lock(outputMutex);
    
    if (outputBuffer.empty()) {
        return false;
    }

    unsigned int samplesToCopy = std::min(maxSamples, static_cast<unsigned int>(outputBuffer.size()));
    std::copy(outputBuffer.begin(), outputBuffer.begin() + samplesToCopy, output);
    outputBuffer.erase(outputBuffer.begin(), outputBuffer.begin() + samplesToCopy);

    return samplesToCopy > 0;
}

void MumbleEchoProcessor::reset() {
    resync.reset();
    
    if (sesEcho) {
        speex_echo_state_destroy(sesEcho);
        sesEcho = nullptr;
    }
    
    if (preprocessState) {
        speex_preprocess_state_destroy(preprocessState);
        preprocessState = nullptr;
    }
    
    std::lock_guard<std::mutex> lock(outputMutex);
    outputBuffer.clear();
    
    processedFrames = 0;
    droppedFrames = 0;
} 