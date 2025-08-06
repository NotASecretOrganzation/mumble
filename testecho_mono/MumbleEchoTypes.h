#ifndef MUMBLE_ECHO_TYPES_H
#define MUMBLE_ECHO_TYPES_H

#include <list>
#include <mutex>

/**
 * A chunk of audio data to process
 * This struct wraps pointers to two dynamically allocated arrays, containing
 * PCM samples of microphone and speaker readback data (for echo cancellation).
 * Does not handle pointer ownership, so you'll have to deallocate them yourself.
 */
struct AudioChunk {
    AudioChunk() : mic(nullptr), speaker(nullptr) {}
    explicit AudioChunk(short *mic) : mic(mic), speaker(nullptr) {}
    AudioChunk(short *mic, short *speaker) : mic(mic), speaker(speaker) {}
    bool empty() const { return mic == nullptr; }

    short *mic;     ///< Pointer to microphone samples
    short *speaker; ///< Pointer to speaker samples, nullptr if echo cancellation is disabled
};

/*
 * According to https://www.speex.org/docs/manual/speex-manual/node7.html
 * "It is important that, at any time, any echo that is present in the input
 * has already been sent to the echo canceller as echo_frame."
 * Thus, we artificially introduce a small lag in the microphone by means of
 * a queue, so as to be sure the speaker data always precedes the microphone.
 *
 * There are conflicting requirements for the queue:
 * - it has to be small enough not to cause a noticeable lag in the voice
 * - it has to be large enough not to force us to drop packets frequently
 *   when the addMic() and addEcho() callbacks are called in a jittery way
 * - its fill level must be controlled so it does not operate towards zero
 *   elements size, as this would not provide the lag required for the
 *   echo canceller to work properly.
 *
 * The current implementation uses a 5 elements queue, with a control
 * statemachine that introduces packet drops to control the fill level
 * to at least 2 (plus or minus one) and less than 4 elements.
 * With a 10ms chunk, this queue should introduce a ~20ms lag to the voice.
 */
class Resynchronizer {
public:
    /**
     * Add a microphone sample to the resynchronizer queue
     * The resynchronizer may decide to drop the sample, and in that case
     * the pointer will be deallocated not to leak memory
     *
     * \param mic pointer to a dynamically allocated array with PCM data
     */
    void addMic(short *mic);

    /**
     * Add a speaker sample to the resynchronizer
     * The resynchronizer may decide to drop the sample, and in that case
     * the pointer will be deallocated not to leak memory
     *
     * \param speaker pointer to a dynamically allocated array with PCM data
     * \return If microphone data is available, the resynchronizer will return a
     * valid audio chunk to encode, otherwise an empty chunk will be returned
     */
    AudioChunk addSpeaker(short *speaker);

    /**
     * Reinitialize the resynchronizer, emptying the queue in the process.
     */
    void reset();

    /**
     * \return the nominal lag that the resynchronizer tries to enforce on the
     * microphone data, in order to make sure the speaker data is always passed
     * first to the echo canceller
     */
    int getNominalLag() const { return 2; }

    ~Resynchronizer();

    bool bDebugPrintQueue = false; ///< Enables printing queue fill level stats

private:
    /**
     * Print queue level stats for debugging purposes
     * \param who used to distinguish between addMic() and addSpeaker()
     */
    void printQueue(char who);

    // TODO: there was a mutex (qmEcho), but can the callbacks be called concurrently?
    mutable std::mutex m;
    std::list<short *> micQueue;                          ///< Queue of microphone samples
    enum { S0, S1a, S1b, S2, S3, S4a, S4b, S5 } state = S0; ///< Queue fill control statemachine
};

// Mumble's exact audio parameters
static const unsigned int SAMPLE_RATE = 48000;  // Mumble uses 48kHz
static const int FRAME_SIZE = 480;              // 10ms at 48kHz = 480 samples
static const int FILTER_LENGTH = 4800;          // 100ms filter = 4800 samples

// Sample format types (like Mumble)
typedef enum { SampleShort, SampleFloat } SampleFormat;

// Mixer function type (like Mumble) - fixed syntax
typedef void (*inMixerFunc)(float*, const void*, unsigned int, unsigned int, unsigned long long);

#endif // MUMBLE_ECHO_TYPES_H 