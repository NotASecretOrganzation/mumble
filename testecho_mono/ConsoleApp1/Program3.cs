using NAudio.Wave;
using SpeexDSPSharp.Core;
using System.Collections.Concurrent;

namespace MumbleEchoCancellation
{
    // Mumble-inspired audio configuration
    public class AudioConfig
    {
        public const int SampleRate = 48000;        // Mumble standard
        public const int FrameSizeMs = 10;          // 10ms processing frames
        public const int FrameSize = (SampleRate * FrameSizeMs) / 1000;  // 480 samples
        public const int FilterLengthMs = 100;      // 100ms adaptive filter
        public const int FilterLength = (SampleRate * FilterLengthMs) / 1000;  // 4800 samples
        public const int Channels = 1;              // Mono processing
        public const int BitsPerSample = 16;        // 16-bit audio
        public const int BufferSize = FrameSize * 2; // Bytes per frame
    }

    // Mumble-inspired audio frame structure
    public class AudioFrame
    {
        public byte[] Data { get; set; }
        public long Timestamp { get; set; }
        public int FrameNumber { get; set; }

        public AudioFrame(byte[] data, long timestamp, int frameNumber)
        {
            Data = data;
            Timestamp = timestamp;
            FrameNumber = frameNumber;
        }
    }

    // Mumble-inspired resynchronizer for mic/speaker alignment
    public class AudioResynchronizer
    {
        private readonly ConcurrentQueue<AudioFrame> _micQueue = new();
        private readonly ConcurrentQueue<AudioFrame> _speakerQueue = new();
        private readonly int _maxQueueSize;
        private int _micFrameCounter = 0;
        private int _speakerFrameCounter = 0;

        public AudioResynchronizer(int maxQueueSize = 100)
        {
            _maxQueueSize = maxQueueSize;
        }

        public void AddMicFrame(byte[] data)
        {
            var frame = new AudioFrame(data, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), _micFrameCounter++);
            _micQueue.Enqueue(frame);

            // Maintain queue size
            while (_micQueue.Count > _maxQueueSize)
            {
                _micQueue.TryDequeue(out _);
            }
        }

        public void AddSpeakerFrame(byte[] data)
        {
            var frame = new AudioFrame(data, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), _speakerFrameCounter++);
            _speakerQueue.Enqueue(frame);

            // Maintain queue size
            while (_speakerQueue.Count > _maxQueueSize)
            {
                _speakerQueue.TryDequeue(out _);
            }
        }

        public (AudioFrame? mic, AudioFrame? speaker) GetSynchronizedFrames()
        {
            if (_micQueue.TryDequeue(out var micFrame) && _speakerQueue.TryDequeue(out var speakerFrame))
            {
                return (micFrame, speakerFrame);
            }
            else if (_micQueue.TryDequeue(out micFrame))
            {
                // No speaker frame available, create silent frame
                var silentFrame = new AudioFrame(new byte[AudioConfig.BufferSize], micFrame.Timestamp, _speakerFrameCounter++);
                return (micFrame, silentFrame);
            }

            return (null, null);
        }

        public int MicQueueCount => _micQueue.Count;
        public int SpeakerQueueCount => _speakerQueue.Count;
    }

    // Mumble-inspired echo cancellation engine
    public class MumbleEchoEngine : IDisposable
    {
        private readonly SpeexDSPEchoCanceler _echoCanceller;
        private readonly SpeexDSPPreprocessor _preprocessor;
        private readonly WaveFormat _waveFormat;
        private bool _disposed = false;

        public MumbleEchoEngine()
        {
            _waveFormat = new WaveFormat(AudioConfig.SampleRate, AudioConfig.BitsPerSample, AudioConfig.Channels);

            // Initialize echo canceller with Mumble parameters
            _echoCanceller = new SpeexDSPEchoCanceler(AudioConfig.FrameSize, AudioConfig.FilterLength);

            // Set sampling rate
            int sampleRate = AudioConfig.SampleRate;
            _echoCanceller.Ctl(EchoCancellationCtl.SPEEX_ECHO_SET_SAMPLING_RATE, ref sampleRate);

            // Initialize preprocessor
            _preprocessor = new SpeexDSPPreprocessor(AudioConfig.FrameSize, AudioConfig.SampleRate);

            // Configure preprocessor with Mumble settings
            ConfigurePreprocessor();

            Console.WriteLine($"Mumble Echo Engine initialized:");
            Console.WriteLine($"  Sample Rate: {AudioConfig.SampleRate}Hz");
            Console.WriteLine($"  Frame Size: {AudioConfig.FrameSize} samples ({AudioConfig.FrameSizeMs}ms)");
            Console.WriteLine($"  Filter Length: {AudioConfig.FilterLength} samples ({AudioConfig.FilterLengthMs}ms)");
        }

        private void ConfigurePreprocessor()
        {
            try
            {
                // Mumble's preprocessor configuration
                int denoise = 1;        // Enable noise suppression
                int agc = 1;            // Enable automatic gain control
                int vad = 0;            // Disable VAD (voice activity detection)
                int agcLevel = 8000;    // AGC target level
                int agcMaxGain = 20000; // AGC maximum gain
                int agcIncrement = 12;  // AGC increment rate
                int agcDecrement = -40; // AGC decrement rate

                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_DENOISE, ref denoise);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC, ref agc);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_VAD, ref vad);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_TARGET, ref agcLevel);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, ref agcMaxGain);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_INCREMENT, ref agcIncrement);
                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_DECREMENT, ref agcDecrement);

                Console.WriteLine("Preprocessor configured with Mumble settings");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Warning: Preprocessor configuration failed: {ex.Message}");
            }
        }

        public byte[] ProcessFrame(byte[] micData, byte[] speakerData)
        {
            if (_disposed) throw new ObjectDisposedException(nameof(MumbleEchoEngine));

            // Ensure correct buffer sizes
            var expectedSize = AudioConfig.BufferSize;
            var micBuffer = micData.Length == expectedSize ? micData : new byte[expectedSize];
            var speakerBuffer = speakerData.Length == expectedSize ? speakerData : new byte[expectedSize];
            var outputBuffer = new byte[expectedSize];

            // Copy data if resizing was needed
            if (micData.Length != expectedSize)
            {
                Array.Copy(micData, micBuffer, Math.Min(micData.Length, expectedSize));
            }
            if (speakerData.Length != expectedSize)
            {
                Array.Copy(speakerData, speakerBuffer, Math.Min(speakerData.Length, expectedSize));
            }

            // Perform echo cancellation (Mumble's core algorithm)
            _echoCanceller.EchoCancel(micBuffer, speakerData, outputBuffer);

            // Apply preprocessing (noise reduction, AGC)
            _preprocessor.Run(outputBuffer);

            return outputBuffer;
        }

        public void FeedSpeakerReference(byte[] speakerData)
        {
            if (_disposed) throw new ObjectDisposedException(nameof(MumbleEchoEngine));
            _echoCanceller.EchoPlayback(speakerData);
        }

        public WaveFormat WaveFormat => _waveFormat;

        public void Dispose()
        {
            if (!_disposed)
            {
                _echoCanceller?.Dispose();
                _preprocessor?.Dispose();
                _disposed = true;
            }
        }
    }

    // Mumble-inspired audio capture manager
    public class AudioCaptureManager : IDisposable
    {
        private readonly WasapiLoopbackCapture _systemCapture;
        private readonly WaveInEvent _micCapture;
        private readonly MediaFoundationResampler _systemResampler;
        private readonly BufferedWaveProvider _systemBuffer;
        private readonly WaveFormat _targetFormat;
        private bool _disposed = false;

        public event Action<byte[]>? SystemAudioReceived;
        public event Action<byte[]>? MicrophoneAudioReceived;

        public AudioCaptureManager()
        {
            _targetFormat = new WaveFormat(AudioConfig.SampleRate, AudioConfig.BitsPerSample, AudioConfig.Channels);

            // Initialize system audio capture (speaker monitoring)
            _systemCapture = new WasapiLoopbackCapture();
            _systemBuffer = new BufferedWaveProvider(_systemCapture.WaveFormat)
            {
                BufferLength = AudioConfig.BufferSize * 200,
                DiscardOnBufferOverflow = true
            };
            _systemResampler = new MediaFoundationResampler(_systemBuffer, _targetFormat);

            // Initialize microphone capture
            _micCapture = new WaveInEvent
            {
                WaveFormat = _targetFormat,
                BufferMilliseconds = AudioConfig.FrameSizeMs
            };

            // Set up event handlers
            _systemCapture.DataAvailable += OnSystemAudioDataAvailable;
            _micCapture.DataAvailable += OnMicrophoneDataAvailable;

            Console.WriteLine($"Audio Capture Manager initialized:");
            Console.WriteLine($"  System Audio Format: {_systemCapture.WaveFormat}");
            Console.WriteLine($"  Microphone Format: {_micCapture.WaveFormat}");
            Console.WriteLine($"  Target Format: {_targetFormat}");
        }

        private void OnSystemAudioDataAvailable(object? sender, WaveInEventArgs e)
        {
            try
            {
                // Add to buffer for resampling
                _systemBuffer.AddSamples(e.Buffer, 0, e.BytesRecorded);

                // Resample to target format
                var resampledBuffer = new byte[AudioConfig.BufferSize];
                int bytesRead = _systemResampler.Read(resampledBuffer, 0, AudioConfig.BufferSize);

                if (bytesRead > 0)
                {
                    // Ensure correct size
                    if (bytesRead != AudioConfig.BufferSize)
                    {
                        Array.Resize(ref resampledBuffer, AudioConfig.BufferSize);
                    }

                    SystemAudioReceived?.Invoke(resampledBuffer);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error processing system audio: {ex.Message}");
            }
        }

        private void OnMicrophoneDataAvailable(object? sender, WaveInEventArgs e)
        {
            try
            {
                var micBuffer = new byte[AudioConfig.BufferSize];
                Array.Copy(e.Buffer, micBuffer, Math.Min(e.BytesRecorded, AudioConfig.BufferSize));

                // Ensure correct size
                if (e.BytesRecorded != AudioConfig.BufferSize)
                {
                    Array.Resize(ref micBuffer, AudioConfig.BufferSize);
                }

                MicrophoneAudioReceived?.Invoke(micBuffer);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error processing microphone audio: {ex.Message}");
            }
        }

        public void Start()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(AudioCaptureManager));

            _systemCapture.StartRecording();
            _micCapture.StartRecording();

            Console.WriteLine("Audio capture started");
        }

        public void Stop()
        {
            if (_disposed) return;

            _systemCapture.StopRecording();
            _micCapture.StopRecording();

            Console.WriteLine("Audio capture stopped");
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                Stop();
                _systemCapture?.Dispose();
                _micCapture?.Dispose();
                _systemResampler?.Dispose();
                _disposed = true;
            }
        }
    }

    // Mumble-inspired audio output manager
    public class AudioOutputManager : IDisposable
    {
        private readonly WaveOutEvent _output;
        private readonly BufferedWaveProvider _playbackBuffer;
        private readonly WaveFileWriter _cleanWriter;
        private readonly WaveFileWriter _originalWriter;
        private readonly string _cleanFileName;
        private readonly string _originalFileName;
        private bool _disposed = false;

        public AudioOutputManager()
        {
            var format = new WaveFormat(AudioConfig.SampleRate, AudioConfig.BitsPerSample, AudioConfig.Channels);

            _output = new WaveOutEvent();
            _playbackBuffer = new BufferedWaveProvider(format)
            {
                BufferLength = AudioConfig.BufferSize * 200,
                DiscardOnBufferOverflow = true
            };
            _output.Init(_playbackBuffer);

            // Create output files
            var timestamp = DateTime.Now.ToString("HHmmss");
            _cleanFileName = $"mumble_clean_{AudioConfig.SampleRate}_{timestamp}.wav";
            _originalFileName = $"mumble_original_{AudioConfig.SampleRate}_{timestamp}.wav";

            _cleanWriter = new WaveFileWriter(_cleanFileName, format);
            _originalWriter = new WaveFileWriter(_originalFileName, format);

            Console.WriteLine($"Audio Output Manager initialized:");
            Console.WriteLine($"  Clean audio file: {_cleanFileName}");
            Console.WriteLine($"  Original audio file: {_originalFileName}");
        }

        public void PlayCleanAudio(byte[] audioData)
        {
            if (_disposed) return;

            _playbackBuffer.AddSamples(audioData, 0, audioData.Length);
            _cleanWriter.Write(audioData, 0, audioData.Length);
        }

        public void SaveOriginalAudio(byte[] audioData)
        {
            if (_disposed) return;

            _originalWriter.Write(audioData, 0, audioData.Length);
        }

        public void Start()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(AudioOutputManager));

            _output.Play();
            Console.WriteLine("Audio output started");
        }

        public void Stop()
        {
            if (_disposed) return;

            _output.Stop();
            Console.WriteLine("Audio output stopped");
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                Stop();
                _output?.Dispose();
                _cleanWriter?.Dispose();
                _originalWriter?.Dispose();
                _disposed = true;
            }
        }

        public string CleanFileName => _cleanFileName;
        public string OriginalFileName => _originalFileName;
    }

    // Main program class
    public class Program
    {
        private static readonly CancellationTokenSource _cancellationTokenSource = new();
        private static readonly AudioResynchronizer _resynchronizer = new(50);
        private static readonly MumbleEchoEngine _echoEngine = new();
        private static readonly AudioCaptureManager _captureManager = new();
        private static readonly AudioOutputManager _outputManager = new();

        private static long _processedFrames = 0;
        private static long _droppedFrames = 0;
        private static long _echoReductionFrames = 0;
        private static readonly object _statsLock = new();

        public static async Task Main(string[] args)
        {
            Console.WriteLine("=========================================");
            Console.WriteLine("Mumble-Inspired Echo Cancellation System");
            Console.WriteLine("=========================================");

            try
            {
                // Set up event handlers
                _captureManager.SystemAudioReceived += OnSystemAudioReceived;
                _captureManager.MicrophoneAudioReceived += OnMicrophoneAudioReceived;

                // Start processing tasks
                var processingTask = Task.Run(ProcessingLoop);
                var statisticsTask = Task.Run(StatisticsLoop);

                // Start audio components
                _captureManager.Start();
                _outputManager.Start();

                Console.WriteLine("=========================================");
                Console.WriteLine("Echo cancellation system is running...");
                Console.WriteLine("Press Enter to stop...");
                Console.WriteLine("=========================================");

                Console.ReadLine();

                // Stop the system
                _cancellationTokenSource.Cancel();
                await Task.WhenAll(processingTask, statisticsTask);

                // Display final statistics
                DisplayFinalStatistics();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
                Console.WriteLine($"Stack trace: {ex.StackTrace}");
            }
            finally
            {
                // Cleanup
                _captureManager?.Dispose();
                _outputManager?.Dispose();
                _echoEngine?.Dispose();
            }
        }

        private static void OnSystemAudioReceived(byte[] audioData)
        {
            _resynchronizer.AddSpeakerFrame(audioData);
            _echoEngine.FeedSpeakerReference(audioData);
        }

        private static void OnMicrophoneAudioReceived(byte[] audioData)
        {
            _resynchronizer.AddMicFrame(audioData);
        }

        private static async Task ProcessingLoop()
        {
            Console.WriteLine("Audio processing loop started");

            while (!_cancellationTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    var (micFrame, speakerFrame) = _resynchronizer.GetSynchronizedFrames();

                    if (micFrame != null)
                    {
                        // Save original audio
                        _outputManager.SaveOriginalAudio(micFrame.Data);

                        // Process with echo cancellation
                        var cleanAudio = _echoEngine.ProcessFrame(micFrame.Data, speakerFrame?.Data ?? new byte[AudioConfig.BufferSize]);

                        // Play and save clean audio
                        _outputManager.PlayCleanAudio(cleanAudio);

                        // Calculate echo reduction
                        var echoReduction = CalculateEchoReduction(micFrame.Data, cleanAudio);
                        if (echoReduction > 0)
                        {
                            Interlocked.Increment(ref _echoReductionFrames);
                        }

                        Interlocked.Increment(ref _processedFrames);

                        // Log every 100 frames
                        if (Interlocked.Read(ref _processedFrames) % 100 == 0)
                        {
                            Console.WriteLine($"Processed {Interlocked.Read(ref _processedFrames)} frames - " +
                                             $"Echo Reduction: {echoReduction:F2}dB - " +
                                             $"Mic Queue: {_resynchronizer.MicQueueCount}, " +
                                             $"Speaker Queue: {_resynchronizer.SpeakerQueueCount}");
                        }
                    }
                    else
                    {
                        // No data available, wait a bit
                        await Task.Delay(1, _cancellationTokenSource.Token);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error in processing loop: {ex.Message}");
                    await Task.Delay(10, _cancellationTokenSource.Token);
                }
            }
        }

        private static async Task StatisticsLoop()
        {
            Console.WriteLine("Statistics reporting loop started");
            var reportCount = 0;

            while (!_cancellationTokenSource.Token.IsCancellationRequested)
            {
                await Task.Delay(5000, _cancellationTokenSource.Token);
                reportCount++;

                var processed = Interlocked.Read(ref _processedFrames);
                var dropped = Interlocked.Read(ref _droppedFrames);
                var echoReduction = Interlocked.Read(ref _echoReductionFrames);

                Console.WriteLine($"=== Status Report #{reportCount} ===");
                Console.WriteLine($"Processed frames: {processed}");
                Console.WriteLine($"Dropped frames: {dropped}");
                Console.WriteLine($"Drop rate: {(processed > 0 ? (double)dropped / processed * 100 : 0):F2}%");
                Console.WriteLine($"Echo reduction frames: {echoReduction}");
                Console.WriteLine($"Echo reduction rate: {(processed > 0 ? (double)echoReduction / processed * 100 : 0):F2}%");
                Console.WriteLine($"Mic queue: {_resynchronizer.MicQueueCount}");
                Console.WriteLine($"Speaker queue: {_resynchronizer.SpeakerQueueCount}");
                Console.WriteLine("================================");
            }
        }

        private static double CalculateEchoReduction(byte[] original, byte[] processed)
        {
            if (original.Length != processed.Length || original.Length == 0)
                return 0.0;

            double originalEnergy = 0.0;
            double processedEnergy = 0.0;

            // Calculate energy from 16-bit samples
            for (int i = 0; i < original.Length; i += 2)
            {
                if (i + 1 < original.Length)
                {
                    short originalSample = BitConverter.ToInt16(original, i);
                    short processedSample = BitConverter.ToInt16(processed, i);

                    originalEnergy += originalSample * originalSample;
                    processedEnergy += processedSample * processedSample;
                }
            }

            if (originalEnergy <= 0 || processedEnergy <= 0)
                return 0.0;

            // Calculate echo reduction in dB
            return 10 * Math.Log10(originalEnergy / processedEnergy);
        }

        private static void DisplayFinalStatistics()
        {
            var processed = Interlocked.Read(ref _processedFrames);
            var dropped = Interlocked.Read(ref _droppedFrames);
            var echoReduction = Interlocked.Read(ref _echoReductionFrames);

            Console.WriteLine("=========================================");
            Console.WriteLine("Final Statistics:");
            Console.WriteLine($"  Processed frames: {processed}");
            Console.WriteLine($"  Dropped frames: {dropped}");
            Console.WriteLine($"  Drop rate: {(processed > 0 ? (double)dropped / processed * 100 : 0):F2}%");
            Console.WriteLine($"  Echo reduction frames: {echoReduction}");
            Console.WriteLine($"  Echo reduction rate: {(processed > 0 ? (double)echoReduction / processed * 100 : 0):F2}%");
            Console.WriteLine($"  Clean audio file: {_outputManager.CleanFileName}");
            Console.WriteLine($"  Original audio file: {_outputManager.OriginalFileName}");
            Console.WriteLine("=========================================");
        }
    }
}