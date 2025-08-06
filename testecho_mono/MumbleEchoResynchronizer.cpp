#include "MumbleEchoTypes.h"
#include <iostream>
#include <algorithm>

// Utility function to clamp float samples (like Mumble)
static short clampFloatSample(float v) {
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return static_cast<short>(v);
}

void Resynchronizer::addMic(short *mic) {
    bool drop = false;
    {
        std::unique_lock<std::mutex> l(m);
        micQueue.push_back(mic);
        switch (state) {
            case S0:
                state = S1a;
                break;
            case S1a:
                state = S2;
                break;
            case S1b:
                state = S2;
                break;
            case S2:
                state = S3;
                break;
            case S3:
                state = S4a;
                break;
            case S4a:
                state = S5;
                break;
            case S4b:
                drop = true;
                break;
            case S5:
                drop = true;
                break;
        }
        if (drop) {
            delete[] micQueue.front();
            micQueue.pop_front();
        }
    }
    if (bDebugPrintQueue) {
        if (drop)
            std::cout << "Resynchronizer::addMic(): dropped microphone chunk due to overflow" << std::endl;
        printQueue('+');
    }
}

AudioChunk Resynchronizer::addSpeaker(short *speaker) {
    AudioChunk result;
    bool drop = false;
    {
        std::unique_lock<std::mutex> l(m);
        switch (state) {
            case S0:
                drop = true;
                break;
            case S1a:
                drop = true;
                break;
            case S1b:
                state = S0;
                break;
            case S2:
                state = S1b;
                break;
            case S3:
                state = S2;
                break;
            case S4a:
                state = S3;
                break;
            case S4b:
                state = S3;
                break;
            case S5:
                state = S4b;
                break;
        }
        if (drop == false) {
            result = AudioChunk(micQueue.front(), speaker);
            micQueue.pop_front();
        }
    }
    if (drop)
        delete[] speaker;
    if (bDebugPrintQueue) {
        if (drop)
            std::cout << "Resynchronizer::addSpeaker(): dropped speaker chunk due to underflow" << std::endl;
        printQueue('-');
    }
    return result;
}

void Resynchronizer::reset() {
    if (bDebugPrintQueue)
        std::cout << "Resetting echo queue" << std::endl;
    std::unique_lock<std::mutex> l(m);
    state = S0;
    while (!micQueue.empty()) {
        delete[] micQueue.front();
        micQueue.pop_front();
    }
}

Resynchronizer::~Resynchronizer() {
    reset();
}

void Resynchronizer::printQueue(char who) {
    unsigned int mic;
    {
        std::unique_lock<std::mutex> l(m);
        mic = static_cast<unsigned int>(micQueue.size());
    }
    std::cout << "Resynchronizer::printQueue(" << who << "): micQueue.size()=" << mic << ", state=" << state << std::endl;
} 