package org.codeshipping.llamakotlin

import org.codeshipping.llamakotlin.exception.LlamaException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.io.Closeable
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Main entry point for LLaMA model inference on Android.
 *
 * This class provides a Kotlin-first API with full coroutine support for
 * loading and running LLaMA models on-device using llama.cpp.
 *
 * ## Basic Usage
 *
 * ```kotlin
 * // Load a model
 * val model = LlamaModel.load("/path/to/model.gguf") {
 *     contextSize = 2048
 *     threads = 4
 * }
 *
 * // Generate text
 * val response = model.generate("Hello, how are you?")
 *
 * // Stream tokens
 * model.generateStream("Tell me a story").collect { token ->
 *     print(token)
 * }
 *
 * // Clean up
 * model.close()
 * ```
 *
 * ## With ViewModel
 *
 * ```kotlin
 * class ChatViewModel : ViewModel() {
 *     private var model: LlamaModel? = null
 *
 *     fun loadModel(path: String) {
 *         viewModelScope.launch {
 *             model = LlamaModel.load(path)
 *         }
 *     }
 *
 *     override fun onCleared() {
 *         model?.close()
 *     }
 * }
 * ```
 *
 * @see LlamaConfig for configuration options
 * @see LlamaException for error handling
 */
class LlamaModel private constructor(
    private val nativeHandle: Long,
    private val _config: LlamaConfig
) : Closeable {

    private val isClosed = AtomicBoolean(false)
    private val isGeneratingFlag = AtomicBoolean(false)

    /**
     * Configuration used to load this model.
     */
    val config: LlamaConfig
        get() = _config.copy()

    /**
     * Check if a model is loaded and ready for inference.
     */
    val isLoaded: Boolean
        get() {
            ensureNotClosed()
            return LlamaNative.nativeIsModelLoaded(nativeHandle)
        }

    /**
     * Check if generation is currently in progress.
     */
    val isGenerating: Boolean
        get() = isGeneratingFlag.get() || LlamaNative.nativeIsGenerating(nativeHandle)

    /**
     * Generate a complete response for the given prompt.
     *
     * This is a suspend function that runs inference on a background thread.
     * It can be safely called from any coroutine context.
     *
     * @param prompt The input text prompt
     * @param configOverride Optional configuration override for this generation
     * @return Complete generated text response
     * @throws LlamaException.ModelNotLoaded if no model is loaded
     * @throws LlamaException.GenerationError if generation fails
     * @throws CancellationException if the coroutine is cancelled
     *
     * Example:
     * ```kotlin
     * val response = model.generate("What is the capital of France?")
     * println(response) // "The capital of France is Paris."
     * ```
     */
    suspend fun generate(
        prompt: String,
        configOverride: LlamaConfig? = null
    ): String = withContext(Dispatchers.Default) {
        ensureNotClosed()
        ensureModelLoaded()

        if (isGeneratingFlag.getAndSet(true)) {
            throw LlamaException.GenerationError("Generation already in progress")
        }

        try {
            val nativeConfig = configOverride?.let {
                it.validate()
                LlamaNative.NativeConfig.fromLlamaConfig(it)
            }

            val result = LlamaNative.nativeGenerate(nativeHandle, prompt, nativeConfig)
            
            // Check for cancellation
            if (!isActive) {
                throw CancellationException("Generation cancelled")
            }
            
            result
        } catch (e: Exception) {
            when (e) {
                is LlamaException -> throw e
                is CancellationException -> {
                    LlamaNative.nativeCancelGeneration(nativeHandle)
                    throw e
                }
                else -> throw LlamaException.GenerationError(e.message ?: "Unknown error", e)
            }
        } finally {
            isGeneratingFlag.set(false)
        }
    }

    /**
     * Generate a streaming response, emitting tokens as they are generated.
     *
     * This function returns a [Flow] that emits each token as it's generated.
     * The flow runs on [Dispatchers.Default] and can be safely collected from any context.
     *
     * @param prompt The input text prompt
     * @param configOverride Optional configuration override for this generation
     * @return Flow of generated tokens
     *
     * Example:
     * ```kotlin
     * model.generateStream("Once upon a time")
     *     .collect { token ->
     *         print(token) // Print each token as it arrives
     *     }
     * ```
     *
     * Example with error handling:
     * ```kotlin
     * model.generateStream(prompt)
     *     .catch { e -> handleError(e) }
     *     .collect { token -> appendToUI(token) }
     * ```
     */
    fun generateStream(
        prompt: String,
        configOverride: LlamaConfig? = null
    ): Flow<String> = callbackFlow {
        ensureNotClosed()
        ensureModelLoaded()

        if (isGeneratingFlag.getAndSet(true)) {
            throw LlamaException.GenerationError("Generation already in progress")
        }

        val nativeConfig = configOverride?.let {
            it.validate()
            LlamaNative.NativeConfig.fromLlamaConfig(it)
        }

        val callback = object : LlamaNative.NativeTokenCallback {
            override fun onToken(token: String) {
                if (isActive) {
                    trySend(token)
                }
            }
        }

        try {
            // Run generation on background thread
            withContext(Dispatchers.Default) {
                LlamaNative.nativeGenerateStream(nativeHandle, prompt, callback, nativeConfig)
            }
        } catch (e: Exception) {
            when (e) {
                is LlamaException -> throw e
                is CancellationException -> {
                    LlamaNative.nativeCancelGeneration(nativeHandle)
                    throw e
                }
                else -> throw LlamaException.GenerationError(e.message ?: "Unknown error", e)
            }
        } finally {
            isGeneratingFlag.set(false)
        }

        // Close the channel when done
        close()

        awaitClose {
            // Cancel generation if flow is cancelled
            if (isGeneratingFlag.get()) {
                LlamaNative.nativeCancelGeneration(nativeHandle)
                isGeneratingFlag.set(false)
            }
        }
    }.flowOn(Dispatchers.Default)

    /**
     * Cancel any ongoing generation.
     *
     * This is safe to call even if no generation is in progress.
     * After cancellation, [LlamaException.GenerationCancelled] will be thrown
     * by the active generation coroutine.
     */
    fun cancelGeneration() {
        if (!isClosed.get()) {
            LlamaNative.nativeCancelGeneration(nativeHandle)
        }
    }

    /**
     * Release native resources.
     *
     * After calling this method, the model instance cannot be used.
     * This method is idempotent - calling it multiple times is safe.
     *
     * It's recommended to use Kotlin's `use` function for automatic cleanup:
     * ```kotlin
     * LlamaModel.load(path).use { model ->
     *     model.generate("Hello")
     * } // Automatically closed here
     * ```
     */
    override fun close() {
        if (isClosed.compareAndSet(false, true)) {
            LlamaNative.nativeCancelGeneration(nativeHandle)
            LlamaNative.nativeDestroyContext(nativeHandle)
        }
    }

    private fun ensureNotClosed() {
        if (isClosed.get()) {
            throw LlamaException.ContextClosed()
        }
    }

    private fun ensureModelLoaded() {
        if (!LlamaNative.nativeIsModelLoaded(nativeHandle)) {
            throw LlamaException.ModelNotLoaded()
        }
    }

    companion object {
        /**
         * Native context counter for debugging
         */
        private val contextCounter = AtomicLong(0)

        init {
            // Ensure native library is loaded
            LlamaNative.ensureLoaded()
        }

        /**
         * Get the library version string.
         *
         * @return Version string (e.g., "0.1.0 (llama.cpp build 1234)")
         */
        @JvmStatic
        fun getVersion(): String = LlamaNative.nativeGetVersion()

        /**
         * Load a GGUF model from the specified path.
         *
         * This is a suspend function that loads the model on a background thread.
         * Model loading can take several seconds depending on model size and device.
         *
         * @param modelPath Absolute path to the .gguf model file
         * @param config Configuration for model loading and inference
         * @return Loaded [LlamaModel] instance
         * @throws LlamaException.ModelNotFound if the model file doesn't exist
         * @throws LlamaException.ModelLoadError if loading fails
         * @throws LlamaException.InvalidConfig if configuration is invalid
         *
         * Example:
         * ```kotlin
         * val model = LlamaModel.load("/sdcard/models/llama-3.2-1b.gguf") {
         *     contextSize = 4096
         *     threads = 4
         *     temperature = 0.7f
         * }
         * ```
         */
        @JvmStatic
        suspend fun load(
            modelPath: String,
            config: LlamaConfig.() -> Unit = {}
        ): LlamaModel = load(modelPath, LlamaConfig().apply(config))

        /**
         * Load a GGUF model with explicit configuration.
         *
         * @param modelPath Absolute path to the .gguf model file
         * @param config Configuration for model loading
         * @return Loaded [LlamaModel] instance
         */
        @JvmStatic
        suspend fun load(
            modelPath: String,
            config: LlamaConfig
        ): LlamaModel = withContext(Dispatchers.IO) {
            // Validate path
            val file = File(modelPath)
            if (!file.exists()) {
                throw LlamaException.ModelNotFound(modelPath)
            }
            if (!file.canRead()) {
                throw LlamaException.ModelLoadError("Cannot read model file: $modelPath")
            }

            // Validate config
            config.validate()

            // Create native context
            val handle = LlamaNative.nativeCreateContext()
            if (handle == 0L) {
                throw LlamaException.NativeError(-1, "Failed to create native context")
            }

            contextCounter.incrementAndGet()

            try {
                // Load model
                val nativeConfig = LlamaNative.NativeConfig.fromLlamaConfig(config)
                val success = LlamaNative.nativeLoadModel(handle, modelPath, nativeConfig)

                if (!success) {
                    val error = LlamaNative.nativeGetLastError(handle)
                    throw LlamaException.ModelLoadError(error.ifEmpty { "Unknown error" })
                }

                LlamaModel(handle, config)
            } catch (e: Exception) {
                // Clean up on failure
                LlamaNative.nativeDestroyContext(handle)
                contextCounter.decrementAndGet()
                
                when (e) {
                    is LlamaException -> throw e
                    else -> throw LlamaException.ModelLoadError(e.message ?: "Unknown error", e)
                }
            }
        }

        /**
         * Load a model from Android assets or resources.
         *
         * Note: This requires copying the model to a readable location first,
         * as assets cannot be read directly by native code.
         *
         * @param modelPath Path relative to assets folder
         * @param context Android context for asset access
         * @param config Configuration for model loading
         * @return Loaded [LlamaModel] instance
         */
        // Note: Implementation would require copying to cache directory first
        // suspend fun loadFromAssets(
        //     assetPath: String,
        //     context: Context,
        //     config: LlamaConfig.() -> Unit = {}
        // ): LlamaModel
    }
}
