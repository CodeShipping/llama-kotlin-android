package org.codeshipping.llamakotlin

import androidx.annotation.Keep

/**
 * Internal JNI bridge to native llama.cpp implementation.
 * This class should not be used directly - use [LlamaModel] instead.
 */
@Keep
internal object LlamaNative {

    /**
     * Flag indicating if native library is loaded.
     */
    @Volatile
    private var isLoaded = false

    /**
     * Load the native library.
     * Thread-safe - can be called multiple times safely.
     */
    @Synchronized
    fun ensureLoaded() {
        if (!isLoaded) {
            System.loadLibrary("llama-android")
            isLoaded = true
        }
    }

    init {
        ensureLoaded()
    }

    // ========================================================================
    // Version Information
    // ========================================================================

    /**
     * Get the native library version string.
     */
    @JvmStatic
    external fun nativeGetVersion(): String

    // ========================================================================
    // Context Management
    // ========================================================================

    /**
     * Create a new native context.
     * @return Context handle (pointer)
     */
    @JvmStatic
    external fun nativeCreateContext(): Long

    /**
     * Destroy a native context and free resources.
     * @param handle Context handle
     */
    @JvmStatic
    external fun nativeDestroyContext(handle: Long)

    // ========================================================================
    // Model Loading
    // ========================================================================

    /**
     * Load a model from file.
     * @param handle Context handle
     * @param modelPath Path to the .gguf model file
     * @param config Configuration object
     * @return true if successful
     * @throws org.codeshipping.llamakotlin.exception.LlamaException on failure
     */
    @JvmStatic
    external fun nativeLoadModel(
        handle: Long,
        modelPath: String,
        config: NativeConfig
    ): Boolean

    /**
     * Unload the current model.
     * @param handle Context handle
     */
    @JvmStatic
    external fun nativeUnloadModel(handle: Long)

    /**
     * Check if a model is loaded.
     * @param handle Context handle
     * @return true if model is loaded
     */
    @JvmStatic
    external fun nativeIsModelLoaded(handle: Long): Boolean

    // ========================================================================
    // Text Generation
    // ========================================================================

    /**
     * Generate text from a prompt (blocking).
     * @param handle Context handle
     * @param prompt Input text
     * @param config Optional config override
     * @return Generated text
     * @throws org.codeshipping.llamakotlin.exception.LlamaException on failure
     */
    @JvmStatic
    external fun nativeGenerate(
        handle: Long,
        prompt: String,
        config: NativeConfig?
    ): String

    /**
     * Generate text with streaming callback.
     * @param handle Context handle
     * @param prompt Input text
     * @param callback Callback for each token
     * @param config Optional config override
     * @throws org.codeshipping.llamakotlin.exception.LlamaException on failure
     */
    @JvmStatic
    external fun nativeGenerateStream(
        handle: Long,
        prompt: String,
        callback: NativeTokenCallback,
        config: NativeConfig?
    )

    // ========================================================================
    // Generation Control
    // ========================================================================

    /**
     * Cancel ongoing generation.
     * @param handle Context handle
     */
    @JvmStatic
    external fun nativeCancelGeneration(handle: Long)

    /**
     * Check if generation is in progress.
     * @param handle Context handle
     * @return true if generating
     */
    @JvmStatic
    external fun nativeIsGenerating(handle: Long): Boolean

    // ========================================================================
    // Error Handling
    // ========================================================================

    /**
     * Get the last error message.
     * @param handle Context handle
     * @return Error message or empty string
     */
    @JvmStatic
    external fun nativeGetLastError(handle: Long): String

    // ========================================================================
    // Helper Classes
    // ========================================================================

    /**
     * Native configuration struct matching C++ LlamaConfig.
     * Fields must match llama_context_wrapper.h
     */
    @Keep
    class NativeConfig {
        @JvmField var contextSize: Int = 2048
        @JvmField var batchSize: Int = 512
        @JvmField var threads: Int = 4
        @JvmField var threadsBatch: Int = 4
        @JvmField var temperature: Float = 0.7f
        @JvmField var topP: Float = 0.9f
        @JvmField var topK: Int = 40
        @JvmField var repeatPenalty: Float = 1.1f
        @JvmField var maxTokens: Int = 512
        @JvmField var useMmap: Boolean = true
        @JvmField var useMlock: Boolean = false
        @JvmField var gpuLayers: Int = 0
        @JvmField var seed: Int = -1

        companion object {
            /**
             * Convert from [LlamaConfig] to [NativeConfig].
             */
            fun fromLlamaConfig(config: LlamaConfig): NativeConfig {
                return NativeConfig().apply {
                    contextSize = config.contextSize
                    batchSize = config.batchSize
                    threads = config.threads
                    threadsBatch = config.effectiveThreadsBatch()
                    temperature = config.temperature
                    topP = config.topP
                    topK = config.topK
                    repeatPenalty = config.repeatPenalty
                    maxTokens = config.maxTokens
                    useMmap = config.useMmap
                    useMlock = config.useMlock
                    gpuLayers = config.gpuLayers
                    seed = config.seed
                }
            }
        }
    }

    /**
     * Callback interface for streaming token generation.
     * Called from native code for each generated token.
     */
    @Keep
    interface NativeTokenCallback {
        /**
         * Called for each generated token.
         * @param token The generated token text
         */
        fun onToken(token: String)
    }
}
