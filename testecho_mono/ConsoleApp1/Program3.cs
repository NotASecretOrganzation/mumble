using NAudio.Wave;
using SpeexDSPSharp.Core;

namespace MumbleEchoCancellation
{
    public class Program
    {
        // Mumble's audio parameters
        private const int SAMPLE_RATE = 48000;
        private const int FRAME_SIZE_MS = 10;
        private const int FRAME_SIZE = (SAMPLE_RATE * FRAME_SIZE_MS) / 1000; // 480 samples
        private const int FILTER_LENGTH_MS = 100;
        private const int FILTER_LENGTH = (SAMPLE_RATE * FILTER_LENGTH_MS) / 1000; // 4800 samples
        private const int BUFFER_SIZE = FRAME_SIZE * 2; // 16-bit samples

        // Audio components
        private static WasapiLoopbackCapture? _systemCapture;
        private static WaveInEvent? _micCapture;
        private static WaveOutEvent? _output;
        private static BufferedWaveProvider? _playbackBuffer;
        private static MediaFoundationResampler? _systemResampler;
        private static BufferedWaveProvider? _systemBuffer;

        // SpeexDSP components
        private static SpeexDSPEchoCanceler? _echoCanceller;
        private static SpeexDSPPreprocessor? _preprocessor;

        // Mumble's simple state machine
        private static readonly Queue<byte[]> _micQueue = new();
        private static readonly object _lock = new();
        private static int _state = 0; // 0-5 states like Mumble

        // Statistics
        private static long _processedFrames = 0;
        private static long _droppedFrames = 0;

        private static CancellationTokenSource _cancellationTokenSource = new CancellationTokenSource();

        public static async Task Main(string[] args)
        {
            Console.WriteLine("Mumble Simple Echo Cancellation");
            Console.WriteLine("Press Enter to stop...");

            try
            {
                InitializeAudio();
                InitializeSpeexDSP();
                StartAudio();

                // Start a status monitoring task
                var statusTask = Task.Run(StatusMonitor);

                Console.ReadLine();

                // Stop the system
                _cancellationTokenSource.Cancel();
                await statusTask;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
            }
            finally
            {
                Cleanup();
            }
        }

        private static async Task StatusMonitor()
        {
            var lastProcessed = 0L;
            var lastDropped = 0L;
            var reportCount = 0;

            while (!_cancellationTokenSource.Token.IsCancellationRequested)
            {
                await Task.Delay(2000, _cancellationTokenSource.Token);
                reportCount++;

                var currentProcessed = Interlocked.Read(ref _processedFrames);
                var currentDropped = Interlocked.Read(ref _droppedFrames);
                var processedDelta = currentProcessed - lastProcessed;
                var droppedDelta = currentDropped - lastDropped;

                Console.WriteLine($"=== Status Report #{reportCount} ===");
                Console.WriteLine($"Processed frames: {currentProcessed} (+{processedDelta})");
                Console.WriteLine($"Dropped frames: {currentDropped} (+{droppedDelta})");
                Console.WriteLine($"Mic queue size: {_micQueue.Count}");
                Console.WriteLine($"State machine state: {_state}");
                Console.WriteLine($"Processing rate: {processedDelta / 2.0:F1} frames/sec");
                Console.WriteLine("================================");

                lastProcessed = currentProcessed;
                lastDropped = currentDropped;
            }
        }

        private static void InitializeAudio()
        {
            var targetFormat = new WaveFormat(SAMPLE_RATE, 16, 1);

            // System audio capture (speaker monitoring)
            _systemCapture = new WasapiLoopbackCapture();
            _systemBuffer = new BufferedWaveProvider(_systemCapture.WaveFormat)
            {
                BufferLength = BUFFER_SIZE * 100,
                DiscardOnBufferOverflow = true
            };
            _systemResampler = new MediaFoundationResampler(_systemBuffer, targetFormat);

            // Microphone capture
            _micCapture = new WaveInEvent
            {
                WaveFormat = targetFormat,
                BufferMilliseconds = FRAME_SIZE_MS
            };

            // Audio output
            _output = new WaveOutEvent();
            _playbackBuffer = new BufferedWaveProvider(targetFormat)
            {
                BufferLength = BUFFER_SIZE * 100,
                DiscardOnBufferOverflow = true
            };
            _output.Init(_playbackBuffer);

            // Set up event handlers
            _systemCapture.DataAvailable += OnSystemAudioDataAvailable;
            _micCapture.DataAvailable += OnMicrophoneDataAvailable;

            Console.WriteLine($"Audio initialized - Target: {SAMPLE_RATE}Hz, Frame: {FRAME_SIZE} samples");
        }

        private static void InitializeSpeexDSP()
        {
            // Initialize echo canceller with Mumble parameters
            _echoCanceller = new SpeexDSPEchoCanceler(FRAME_SIZE, FILTER_LENGTH);
            int sampleRate = SAMPLE_RATE;
            _echoCanceller.Ctl(EchoCancellationCtl.SPEEX_ECHO_SET_SAMPLING_RATE, ref sampleRate);

            // Initialize preprocessor
            _preprocessor = new SpeexDSPPreprocessor(FRAME_SIZE, SAMPLE_RATE);

            // Configure preprocessor
            int denoise = 1, agc = 1, vad = 0;
            int agcLevel = 8000, agcMaxGain = 20000;
            int agcIncrement = 12, agcDecrement = -40;

            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_DENOISE, ref denoise);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC, ref agc);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_VAD, ref vad);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_TARGET, ref agcLevel);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, ref agcMaxGain);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_INCREMENT, ref agcIncrement);
            _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_DECREMENT, ref agcDecrement);

            Console.WriteLine("SpeexDSP initialized with Mumble settings");
        }

        private static void StartAudio()
        {
            _systemCapture?.StartRecording();
            _micCapture?.StartRecording();
            _output?.Play();
            Console.WriteLine("Audio started");
        }

        private static void OnSystemAudioDataAvailable(object? sender, WaveInEventArgs e)
        {
            try
            {
                // Add to buffer for resampling
                _systemBuffer?.AddSamples(e.Buffer, 0, e.BytesRecorded);

                // Resample to target format
                if (_systemResampler != null)
                {
                    var resampledBuffer = new byte[BUFFER_SIZE];
                    int bytesRead = _systemResampler.Read(resampledBuffer, 0, BUFFER_SIZE);

                    if (bytesRead > 0)
                    {
                        // Use Mumble's approach: addSpeaker returns synchronized pair
                        var (micData, speakerData) = AddSpeaker(resampledBuffer);
                        
                        if (micData != null && speakerData != null)
                        {
                            ProcessFrame(micData, speakerData);
                        }
                        else
                        {
                            // Log when we don't have synchronized data
                            if (Interlocked.Read(ref _processedFrames) % 50 == 0)
                            {
                                Console.WriteLine($"No synchronized data - Mic Queue: {_micQueue.Count}, State: {_state}");
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"System audio error: {ex.Message}");
            }
        }

        private static void OnMicrophoneDataAvailable(object? sender, WaveInEventArgs e)
        {
            try
            {
                // Use Mumble's approach: just add mic data to queue
                AddMic(e.Buffer);
                
                // Log microphone data arrival
                if (Interlocked.Read(ref _processedFrames) % 50 == 0)
                {
                    Console.WriteLine($"Mic data received - Queue: {_micQueue.Count}, State: {_state}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Microphone error: {ex.Message}");
            }
        }

        // Mumble's simple state machine
        private static void AddMic(byte[] micData)
        {
            bool drop = false;
            lock (_lock)
            {
                _micQueue.Enqueue(micData);
                
                // Simple state machine: 0->1->2->3->4->5 (drop)
                if (_state < 5)
                {
                    _state++;
                }
                else
                {
                    drop = true;
                    _micQueue.Dequeue(); // Remove the frame we just added
                }
            }
            
            if (drop)
            {
                Interlocked.Increment(ref _droppedFrames);
                Console.WriteLine($"Dropped mic frame due to overflow - State: {_state}");
            }
        }

        private static (byte[]? mic, byte[]? speaker) AddSpeaker(byte[] speakerData)
        {
            byte[]? micData = null;
            bool drop = false;
            
            lock (_lock)
            {
                // Simple state machine: 5->4->3->2->1->0 (drop)
                if (_state > 0)
                {
                    _state--;
                    if (_micQueue.Count > 0)
                    {
                        micData = _micQueue.Dequeue();
                    }
                }
                else
                {
                    drop = true;
                }
            }
            
            if (drop)
            {
                Interlocked.Increment(ref _droppedFrames);
                Console.WriteLine($"Dropped speaker frame due to underflow - State: {_state}");
                return (null, null);
            }
            
            return (micData, speakerData);
        }

        private static void ProcessFrame(byte[] micData, byte[] speakerData)
        {
            try
            {
                if (_echoCanceller == null || _preprocessor == null) return;

                var outputBuffer = new byte[BUFFER_SIZE];

                // Perform echo cancellation
                _echoCanceller.EchoCancel(micData, speakerData, outputBuffer);

                // Apply preprocessing
                _preprocessor.Run(outputBuffer);

                // Play clean audio
                _playbackBuffer?.AddSamples(outputBuffer, 0, outputBuffer.Length);

                Interlocked.Increment(ref _processedFrames);

                // Log every 100 frames
                if (Interlocked.Read(ref _processedFrames) % 100 == 0)
                {
                    Console.WriteLine($"Processed {Interlocked.Read(ref _processedFrames)} frames - " +
                                     $"Dropped: {Interlocked.Read(ref _droppedFrames)} - " +
                                     $"Queue: {_micQueue.Count}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Processing error: {ex.Message}");
            }
        }

        private static void Cleanup()
        {
            _systemCapture?.StopRecording();
            _micCapture?.StopRecording();
            _output?.Stop();

            _systemCapture?.Dispose();
            _micCapture?.Dispose();
            _output?.Dispose();
            _systemResampler?.Dispose();
            _echoCanceller?.Dispose();
            _preprocessor?.Dispose();

            Console.WriteLine($"Final stats - Processed: {Interlocked.Read(ref _processedFrames)}, " +
                             $"Dropped: {Interlocked.Read(ref _droppedFrames)}");
        }
    }
}