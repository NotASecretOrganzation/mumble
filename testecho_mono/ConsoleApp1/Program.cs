//using ConsoleApp1;
//using NAudio.Wave;
//using NAudio.CoreAudioApi;
//using SpeexDSPSharp.Core;
//using SpeexDSPSharp.Core.SafeHandlers;
//using System.Collections.Concurrent;

//// Audio parameters based on Mumble's configuration
//const int TARGET_SAMPLE_RATE = 48000;  // Mumble uses 48kHz
//const int FRAME_SIZE_MS = 10;          // 10ms frame
//const int FRAME_SIZE = (TARGET_SAMPLE_RATE * FRAME_SIZE_MS) / 1000;  // 480 samples
//const int FILTER_LENGTH_MS = 100;      // 100ms filter length
//const int FILTER_LENGTH = (TARGET_SAMPLE_RATE * FILTER_LENGTH_MS) / 1000;  // 4800 samples
//const int CHANNELS = 1;                // Mono channel
//const int MAX_BUFFER_SIZE = 50;        // Maximum buffer size

//var format = new WaveFormat(TARGET_SAMPLE_RATE, 16, CHANNELS);
//var bufferSize = FRAME_SIZE * 2; // 16-bit mono = 2 bytes per sample

//Console.WriteLine("=========================================");
//Console.WriteLine("Real-Time Echo Cancellation Demo (C#)");
//Console.WriteLine("Based on Mumble's Echo Cancellation Design");
//Console.WriteLine("=========================================");
//Console.WriteLine($"Target Sample Rate: {TARGET_SAMPLE_RATE}Hz");
//Console.WriteLine($"Frame Size: {FRAME_SIZE} samples ({FRAME_SIZE_MS}ms)");
//Console.WriteLine($"Filter Length: {FILTER_LENGTH} samples ({FILTER_LENGTH_MS}ms)");
//Console.WriteLine($"Channels: {CHANNELS}");
//Console.WriteLine($"Buffer Size: {bufferSize} bytes per frame");
//Console.WriteLine($"Max Buffer Queue Size: {MAX_BUFFER_SIZE} frames");
//Console.WriteLine("=========================================");

//// Echo canceller based on Mumble's design
//Console.WriteLine("Initializing Mumble Echo Cancellation...");
//var echoCanceller = new MumbleEchoCancellation(FRAME_SIZE_MS, FILTER_LENGTH_MS, format);
//Console.WriteLine("Mumble Echo Cancellation initialized successfully!");

//// Audio buffers - based on Mumble's Resynchronizer design
//var micBuffer = new ConcurrentQueue<byte[]>();
//var speakerBuffer = new ConcurrentQueue<byte[]>();
//var outputBuffer = new ConcurrentQueue<byte[]>();

//// Echo cancellation comparison buffers
//var originalMicBuffer = new ConcurrentQueue<byte[]>();
//var echoCancelledBuffer = new ConcurrentQueue<byte[]>();

//// Synchronization mechanism
//var processingCancellationToken = new CancellationTokenSource();
//var micProcessed = new ManualResetEvent(false);
//var speakerProcessed = new ManualResetEvent(false);

//// Statistics
//var processedFrames = 0L;
//var droppedFrames = 0L;
//var echoReductionFrames = 0L;

//// System audio capture (speaker output monitoring)
//Console.WriteLine("Setting up system audio capture (WASAPI Loopback)...");
//var systemAudioCapture = new WasapiLoopbackCapture();
//Console.WriteLine($"System audio format: {systemAudioCapture.WaveFormat}");

//// Create buffer provider for system audio processing
//Console.WriteLine("Creating system audio buffer...");
//var systemAudioBuffer = new BufferedWaveProvider(systemAudioCapture.WaveFormat)
//{
//    BufferLength = bufferSize * 100,
//    DiscardOnBufferOverflow = true
//};
//Console.WriteLine($"System audio buffer size: {systemAudioBuffer.BufferLength} bytes");

//// Create resampler to convert system audio to target format
//Console.WriteLine("Creating system audio resampler...");
//var systemAudioResampler = new MediaFoundationResampler(systemAudioBuffer, format);
//Console.WriteLine($"Resampler: {systemAudioCapture.WaveFormat} -> {format}");

//// Microphone recording device
//Console.WriteLine("Setting up microphone recording...");
//var recorder = new WaveInEvent()
//{
//    WaveFormat = format,
//    BufferMilliseconds = FRAME_SIZE_MS
//};
//Console.WriteLine($"Microphone format: {recorder.WaveFormat}");
//Console.WriteLine($"Microphone buffer: {recorder.BufferMilliseconds}ms");

//// Audio output
//Console.WriteLine("Setting up audio output...");
//var output = new WaveOutEvent();
//var playbackBuffer = new BufferedWaveProvider(format)
//{
//    BufferLength = bufferSize * 100,
//    DiscardOnBufferOverflow = true
//};
//output.Init(playbackBuffer);
//Console.WriteLine($"Output format: {format}");
//Console.WriteLine($"Playback buffer size: {playbackBuffer.BufferLength} bytes");

//// Output WAV files for comparison
//var outputFile = $"aec_clean_{TARGET_SAMPLE_RATE}_{DateTime.Now:HHmmss}.wav";
//var originalFile = $"aec_original_{TARGET_SAMPLE_RATE}_{DateTime.Now:HHmmss}.wav";
//Console.WriteLine($"Output WAV file (echo cancelled): {outputFile}");
//Console.WriteLine($"Output WAV file (original): {originalFile}");
//var writer = new WaveFileWriter(outputFile, format);
//var originalWriter = new WaveFileWriter(originalFile, format);
//Console.WriteLine($"WAV writer format: {writer.WaveFormat}");

//// Audio processing task based on Mumble's design
//Console.WriteLine("Starting audio processing task...");
//var processingTask = Task.Run(async () =>
//{
//    Console.WriteLine("Audio processing task started");
//    Console.WriteLine("Echo cancellation mode: Enabled");
//    Console.WriteLine("Processing loop: Waiting for microphone data...");

//    while (!processingCancellationToken.Token.IsCancellationRequested)
//    {
//        try
//        {
//            // Wait for microphone data - based on Mumble's Resynchronizer logic
//            if (micBuffer.TryDequeue(out var micData))
//            {
//                // Get speaker data
//                byte[] speakerData;
//                if (!speakerBuffer.TryDequeue(out speakerData))
//                {
//                    // No speaker data available, use silence
//                    speakerData = new byte[bufferSize];
//                    Console.WriteLine("WARNING: No speaker data available, using silence");
//                }

//                // Ensure buffer sizes are correct
//                if (micData.Length != bufferSize)
//                {
//                    Array.Resize(ref micData, bufferSize);
//                }
//                if (speakerData.Length != bufferSize)
//                {
//                    Array.Resize(ref speakerData, bufferSize);
//                }

//                var outputData = new byte[bufferSize];

//                // Store original microphone data for comparison
//                var originalMicData = new byte[bufferSize];
//                Buffer.BlockCopy(micData, 0, originalMicData, 0, bufferSize);
//                originalMicBuffer.Enqueue(originalMicData);

//                // Perform echo cancellation - based on Mumble's speex_echo_cancellation
//                echoCanceller.Cancel(micData, speakerData, outputData);

//                // Store echo cancelled data for comparison
//                echoCancelledBuffer.Enqueue(outputData);

//                // Add to output buffer
//                outputBuffer.Enqueue(outputData);

//                // Limit output buffer size
//                while (outputBuffer.Count > MAX_BUFFER_SIZE)
//                {
//                    outputBuffer.TryDequeue(out _);
//                }

//                // Write to WAV files
//                writer.Write(outputData, 0, outputData.Length);
//                originalWriter.Write(originalMicData, 0, originalMicData.Length);

//                // Calculate echo reduction metrics
//                var echoReduction = CalculateEchoReduction(originalMicData, outputData);
//                if (echoReduction > 0)
//                {
//                    Interlocked.Increment(ref echoReductionFrames);
//                }

//                Interlocked.Increment(ref processedFrames);
                
//                // Display detailed information every 100 frames
//                if (Interlocked.Read(ref processedFrames) % 100 == 0)
//                {
//                    Console.WriteLine($"Processed {Interlocked.Read(ref processedFrames)} frames - " +
//                                     $"Mic: {micData.Length} bytes, Speaker: {speakerData.Length} bytes, " +
//                                     $"Output: {outputData.Length} bytes, Echo Reduction: {echoReduction:F2}dB");
//                }
//            }
//            else
//            {
//                // No microphone data, wait a bit
//                await Task.Delay(1, processingCancellationToken.Token);
//            }
//        }
//        catch (Exception ex)
//        {
//            Console.WriteLine($"ERROR in audio processing: {ex.Message}");
//            Console.WriteLine($"Stack trace: {ex.StackTrace}");
//            await Task.Delay(10, processingCancellationToken.Token);
//        }
//    }
//}, processingCancellationToken.Token);

//// System audio processing - based on Mumble's addEcho design
//Console.WriteLine("Setting up system audio processing callback...");
//systemAudioCapture.DataAvailable += (s, e) =>
//{
//    try
//    {
//        // Add captured system audio to buffer
//        systemAudioBuffer.AddSamples(e.Buffer, 0, e.BytesRecorded);

//        // Resample system audio to target format
//        var resampledBuffer = new byte[bufferSize];
//        int resampledBytes = systemAudioResampler.Read(resampledBuffer, 0, bufferSize);

//        if (resampledBytes > 0)
//        {
//            // Ensure buffer size is correct
//            if (resampledBytes != bufferSize)
//            {
//                Array.Resize(ref resampledBuffer, bufferSize);
//                Console.WriteLine($"WARNING: Resampled buffer size mismatch - Expected: {bufferSize}, Got: {resampledBytes}");
//            }

//            // Add to speaker buffer
//            speakerBuffer.Enqueue(resampledBuffer);

//            // Limit buffer size
//            while (speakerBuffer.Count > MAX_BUFFER_SIZE)
//            {
//                speakerBuffer.TryDequeue(out _);
//            }

//            // Provide reference signal to echo canceller
//            echoCanceller.EchoPlayBack(resampledBuffer);
//        }
//        else
//        {
//            Console.WriteLine("WARNING: No resampled system audio data available");
//        }
//    }
//    catch (Exception ex)
//    {
//        Console.WriteLine($"ERROR processing system audio: {ex.Message}");
//        Console.WriteLine($"Stack trace: {ex.StackTrace}");
//    }
//};

//// Microphone recording processing - based on Mumble's AudioInput design
//Console.WriteLine("Setting up microphone processing callback...");
//recorder.DataAvailable += (s, e) =>
//{
//    try
//    {
//        var micData = new byte[bufferSize];
//        Buffer.BlockCopy(e.Buffer, 0, micData, 0, Math.Min(e.BytesRecorded, bufferSize));

//        // Ensure buffer size is correct
//        if (e.BytesRecorded != bufferSize)
//        {
//            Array.Resize(ref micData, bufferSize);
//            Console.WriteLine($"WARNING: Microphone buffer size mismatch - Expected: {bufferSize}, Got: {e.BytesRecorded}");
//        }

//        // Add to microphone buffer
//        micBuffer.Enqueue(micData);

//        // Limit buffer size
//        while (micBuffer.Count > MAX_BUFFER_SIZE)
//        {
//            micBuffer.TryDequeue(out _);
//            Interlocked.Increment(ref droppedFrames);
//            Console.WriteLine("WARNING: Microphone buffer overflow, dropped frame");
//        }
//    }
//    catch (Exception ex)
//    {
//        Console.WriteLine($"ERROR processing microphone audio: {ex.Message}");
//        Console.WriteLine($"Stack trace: {ex.StackTrace}");
//    }
//};

//// Audio output processing
//Console.WriteLine("Starting audio output task...");
//var outputTask = Task.Run(async () =>
//{
//    Console.WriteLine("Audio output task started");
//    var outputFrameCount = 0L;
    
//    while (!processingCancellationToken.Token.IsCancellationRequested)
//    {
//        try
//        {
//            if (outputBuffer.TryDequeue(out var outputData))
//            {
//                playbackBuffer.AddSamples(outputData, 0, outputData.Length);
//                outputFrameCount++;
                
//                // Display output status every 100 frames
//                if (outputFrameCount % 100 == 0)
//                {
//                    Console.WriteLine($"Output: {outputFrameCount} frames sent to playback buffer");
//                }
//            }
//            else
//            {
//                // No output data, wait a bit
//                await Task.Delay(1, processingCancellationToken.Token);
//            }
//        }
//        catch (Exception ex)
//        {
//            Console.WriteLine($"ERROR in output processing: {ex.Message}");
//            Console.WriteLine($"Stack trace: {ex.StackTrace}");
//            await Task.Delay(10, processingCancellationToken.Token);
//        }
//    }
//}, processingCancellationToken.Token);

//// Statistics reporting task
//Console.WriteLine("Starting statistics reporting task...");
//var statsTask = Task.Run(async () =>
//{
//    Console.WriteLine("Statistics reporting task started");
//    var reportCount = 0;
    
//    while (!processingCancellationToken.Token.IsCancellationRequested)
//    {
//        await Task.Delay(5000, processingCancellationToken.Token); // Report every 5 seconds
//        reportCount++;
        
//        var currentProcessed = Interlocked.Read(ref processedFrames);
//        var currentDropped = Interlocked.Read(ref droppedFrames);
//        var currentEchoReduction = Interlocked.Read(ref echoReductionFrames);
        
//        Console.WriteLine($"=== Status Report #{reportCount} ===");
//        Console.WriteLine($"Processed frames: {currentProcessed}");
//        Console.WriteLine($"Dropped frames: {currentDropped}");
//        Console.WriteLine($"Drop rate: {(currentProcessed > 0 ? (double)currentDropped / currentProcessed * 100 : 0):F2}%");
//        Console.WriteLine($"Echo reduction frames: {currentEchoReduction}");
//        Console.WriteLine($"Echo reduction rate: {(currentProcessed > 0 ? (double)currentEchoReduction / currentProcessed * 100 : 0):F2}%");
//        Console.WriteLine($"Buffer status:");
//        Console.WriteLine($"  Microphone buffer: {micBuffer.Count}/{MAX_BUFFER_SIZE} frames");
//        Console.WriteLine($"  Speaker buffer: {speakerBuffer.Count}/{MAX_BUFFER_SIZE} frames");
//        Console.WriteLine($"  Output buffer: {outputBuffer.Count}/{MAX_BUFFER_SIZE} frames");
//        Console.WriteLine($"  Original buffer: {originalMicBuffer.Count}/{MAX_BUFFER_SIZE} frames");
//        Console.WriteLine($"  Echo cancelled buffer: {echoCancelledBuffer.Count}/{MAX_BUFFER_SIZE} frames");
//        Console.WriteLine($"System audio buffer: {systemAudioBuffer.BufferedBytes} bytes");
//        Console.WriteLine($"Playback buffer: {playbackBuffer.BufferedBytes} bytes");
//        Console.WriteLine("================================");
//    }
//}, processingCancellationToken.Token);

//// Start all components
//Console.WriteLine("=========================================");
//Console.WriteLine("Starting all audio components...");
//Console.WriteLine("=========================================");

//Console.WriteLine("Starting system audio capture...");
//systemAudioCapture.StartRecording();
//Console.WriteLine("System audio capture started successfully");

//Console.WriteLine("Starting microphone recording...");
//recorder.StartRecording();
//Console.WriteLine("Microphone recording started successfully");

//Console.WriteLine("Starting audio playback...");
//output.Play();
//Console.WriteLine("Audio playback started successfully");

//Console.WriteLine("=========================================");
//Console.WriteLine("Real-time echo cancellation started successfully!");
//Console.WriteLine("All components are running...");
//Console.WriteLine("Press Enter to stop...");
//Console.WriteLine("=========================================");

//Console.ReadLine();

//// Cleanup
//Console.WriteLine("=========================================");
//Console.WriteLine("Stopping real-time echo cancellation...");
//Console.WriteLine("=========================================");

//Console.WriteLine("Sending cancellation signal to all tasks...");
//processingCancellationToken.Cancel();

//Console.WriteLine("Stopping system audio capture...");
//systemAudioCapture.StopRecording();
//Console.WriteLine("System audio capture stopped");

//Console.WriteLine("Stopping microphone recording...");
//recorder.StopRecording();
//Console.WriteLine("Microphone recording stopped");

//Console.WriteLine("Stopping audio playback...");
//output.Stop();
//Console.WriteLine("Audio playback stopped");

//// Wait for tasks to complete
//Console.WriteLine("Waiting for all tasks to complete...");
//var tasks = new[] { processingTask, outputTask, statsTask };
//var completed = Task.WaitAll(tasks, 5000);
//Console.WriteLine($"All tasks completed: {completed}");

//// Dispose resources
//Console.WriteLine("Disposing resources...");
//systemAudioResampler?.Dispose();
//Console.WriteLine("System audio resampler disposed");

//writer.Dispose();
//originalWriter.Dispose();
//Console.WriteLine("WAV writers disposed");

//echoCanceller.Dispose();
//Console.WriteLine("Echo canceller disposed");

//var finalProcessed = Interlocked.Read(ref processedFrames);
//var finalDropped = Interlocked.Read(ref droppedFrames);
//var finalEchoReduction = Interlocked.Read(ref echoReductionFrames);

//Console.WriteLine("=========================================");
//Console.WriteLine("Real-time echo cancellation stopped.");
//Console.WriteLine("=========================================");
//Console.WriteLine($"Final Statistics:");
//Console.WriteLine($"  Processed frames: {finalProcessed}");
//Console.WriteLine($"  Dropped frames: {finalDropped}");
//Console.WriteLine($"  Drop rate: {(finalProcessed > 0 ? (double)finalDropped / finalProcessed * 100 : 0):F2}%");
//Console.WriteLine($"  Echo reduction frames: {finalEchoReduction}");
//Console.WriteLine($"  Echo reduction rate: {(finalProcessed > 0 ? (double)finalEchoReduction / finalProcessed * 100 : 0):F2}%");
//Console.WriteLine($"  Echo cancelled file: {outputFile}");
//Console.WriteLine($"  Original file: {originalFile}");
//Console.WriteLine($"  Echo cancelled file size: {new FileInfo(outputFile).Length} bytes");
//Console.WriteLine($"  Original file size: {new FileInfo(originalFile).Length} bytes");
//Console.WriteLine("=========================================");

//// Echo reduction calculation function
//static double CalculateEchoReduction(byte[] original, byte[] processed)
//{
//    if (original.Length != processed.Length || original.Length == 0)
//        return 0.0;

//    double originalEnergy = 0.0;
//    double processedEnergy = 0.0;

//    // Convert bytes to 16-bit samples and calculate energy
//    for (int i = 0; i < original.Length; i += 2)
//    {
//        if (i + 1 < original.Length)
//        {
//            short originalSample = BitConverter.ToInt16(original, i);
//            short processedSample = BitConverter.ToInt16(processed, i);
            
//            originalEnergy += originalSample * originalSample;
//            processedEnergy += processedSample * processedSample;
//        }
//    }

//    if (originalEnergy <= 0 || processedEnergy <= 0)
//        return 0.0;

//    // Calculate echo reduction in dB
//    double echoReduction = 10 * Math.Log10(originalEnergy / processedEnergy);
//    return echoReduction;
//}

//namespace ConsoleApp1
//{
//    /// <summary>
//    /// Echo canceller based on Mumble's design
//    /// Uses Mumble's audio parameters and processing logic
//    /// </summary>
//    public class MumbleEchoCancellation : IDisposable
//    {
//        private readonly CustomSpeexDSPEchoCanceler _canceller;
//        private readonly CustomSpeexDSPPreprocessor _preprocessor;
//        private readonly int _frameSize;
//        private readonly int _sampleRate;
//        private readonly WaveFormat _waveFormat;

//        public WaveFormat WaveFormat => _waveFormat;

//        public unsafe MumbleEchoCancellation(int frameSizeMS, int filterLengthMS, WaveFormat format)
//        {
//            _waveFormat = format;
//            _sampleRate = format.SampleRate;

//            // Calculate frame size and filter length (based on Mumble's calculation method)
//            _frameSize = (frameSizeMS * _sampleRate) / 1000;
//            var filterLength = (filterLengthMS * _sampleRate) / 1000;

//            Console.WriteLine($"Initializing Mumble Echo Cancellation:");
//            Console.WriteLine($"  Frame Size: {_frameSize} samples");
//            Console.WriteLine($"  Filter Length: {filterLength} samples");
//            Console.WriteLine($"  Sample Rate: {_sampleRate}Hz");

//            // Initialize echo canceller - based on Mumble's speex_echo_state_init
//            _canceller = new CustomSpeexDSPEchoCanceler(_frameSize, filterLength);

//            // Set sampling rate - based on Mumble's speex_echo_ctl
//            _canceller.Ctl(EchoCancellationCtl.SPEEX_ECHO_SET_SAMPLING_RATE, ref _sampleRate);

//            // Initialize preprocessor - based on Mumble's speex_preprocess_state_init
//            _preprocessor = new CustomSpeexDSPPreprocessor(_frameSize, _sampleRate);

//            // Link echo cancellation and preprocessing - based on Mumble's design
//            var echoStatePtr = _canceller.Handler.DangerousGetHandle();
//            if (NativeSpeexDSP.speex_preprocess_ctl(_preprocessor.Handler, 
//                PreprocessorCtl.SPEEX_PREPROCESS_SET_ECHO_STATE.GetHashCode(), 
//                echoStatePtr.ToPointer()) == 0)
//            {
//                Console.WriteLine("Preprocessor linked with echo canceller successfully.");
//            }

//            // Configure preprocessor - based on Mumble's settings
//            ConfigurePreprocessor();
//        }

//        private void ConfigurePreprocessor()
//        {
//            try
//            {
//                // Preprocessor configuration based on Mumble's settings
//                int denoise = 1;        // Enable noise reduction
//                int agc = 1;            // Enable automatic gain control
//                int vad = 0;            // Disable VAD to avoid warnings
//                int agcLevel = 8000;    // AGC target level
//                int agcMaxGain = 20000; // AGC maximum gain
//                int agcIncrement = 12;  // AGC increment
//                int agcDecrement = -40; // AGC decrement

//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_DENOISE, ref denoise);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC, ref agc);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_VAD, ref vad);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_TARGET, ref agcLevel);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, ref agcMaxGain);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_INCREMENT, ref agcIncrement);
//                _preprocessor.Ctl(PreprocessorCtl.SPEEX_PREPROCESS_SET_AGC_DECREMENT, ref agcDecrement);

//                Console.WriteLine("Preprocessor configured with Mumble settings:");
//                Console.WriteLine($"  Denoise: {denoise}");
//                Console.WriteLine($"  AGC: {agc}");
//                Console.WriteLine($"  VAD: {vad}");
//                Console.WriteLine($"  AGC Target: {agcLevel}");
//                Console.WriteLine($"  AGC Max Gain: {agcMaxGain}");
//            }
//            catch (Exception ex)
//            {
//                Console.WriteLine($"Warning: Could not configure all preprocessor settings: {ex.Message}");
//            }
//        }

//        /// <summary>
//        /// Perform echo cancellation - based on Mumble's speex_echo_cancellation and speex_preprocess_run
//        /// </summary>
//        /// <param name="referenceBuffer">Speaker reference signal</param>
//        /// <param name="capturedBuffer">Microphone captured signal</param>
//        /// <param name="outputBuffer">Echo cancelled output</param>
//        public void Cancel(byte[] referenceBuffer, byte[] capturedBuffer, byte[] outputBuffer)
//        {
//            // Ensure all buffer sizes are correct
//            var frameBytes = _frameSize * 2; // 16-bit = 2 bytes per sample

//            if (referenceBuffer.Length != frameBytes ||
//                capturedBuffer.Length != frameBytes ||
//                outputBuffer.Length != frameBytes)
//            {
//                Array.Resize(ref referenceBuffer, frameBytes);
//                Array.Resize(ref capturedBuffer, frameBytes);
//                Array.Resize(ref outputBuffer, frameBytes);
//            }

//            // Perform echo cancellation - based on Mumble's speex_echo_cancellation
//            _canceller.EchoCancel(referenceBuffer, capturedBuffer, outputBuffer);

//            // Apply preprocessing - based on Mumble's speex_preprocess_run
//            _preprocessor.Run(outputBuffer);
//        }

//        /// <summary>
//        /// Provide reference signal to echo canceller - based on Mumble's addEcho design
//        /// </summary>
//        /// <param name="echoPlayback">System audio buffer</param>
//        public void EchoPlayBack(byte[] echoPlayback)
//        {
//            _canceller.EchoPlayback(echoPlayback);
//        }

//        /// <summary>
//        /// Dispose resources
//        /// </summary>
//        public void Dispose()
//        {
//            _canceller?.Dispose();
//            _preprocessor?.Dispose();
//        }
//    }

//    public class CustomSpeexDSPPreprocessor(int frame_size, int filter_length) : SpeexDSPPreprocessor(frame_size, filter_length)
//    {
//        public SpeexDSPPreprocessStateSafeHandler Handler => base._handler;
//    }

//    public class CustomSpeexDSPEchoCanceler(int frame_size, int filter_length) : SpeexDSPEchoCanceler(frame_size, filter_length)
//    {
//        public SpeexDSPEchoStateSafeHandler Handler => base._handler;
//    }
//}