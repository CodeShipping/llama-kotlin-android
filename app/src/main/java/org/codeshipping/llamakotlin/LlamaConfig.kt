package org.codeshipping.llamakotlin

import org.codeshipping.llamakotlin.exception.LlamaException

/**
 * Configuration class for LLaMA model loading and inference.
 * Supports Kotlin DSL-style configuration.
 *
 * Example usage:
 * ```kotlin
 * val config = LlamaConfig {
 *     contextSize = 2048
 *     threads = 4
 *     temperature = 0.7f
 * }
 * ```
 */
data class LlamaConfig(
    // ========================================================================
    // Context Parameters
    // ========================================================================
    
    /**
     * Maximum context length in tokens.
     * Larger values allow longer conversations but use more memory.
     * Default: 2048
     */
    var contextSize: Int = 2048,

    /**
     * Batch size for prompt processing.
     * Larger values can speed up prompt processing.
     * Default: 512
     */
    var batchSize: Int = 512,

    // ========================================================================
    // Threading
    // ========================================================================

    /**
     * Number of threads to use for inference.
     * Recommended: Number of CPU performance cores.
     * Default: 4
     */
    var threads: Int = 4,

    /**
     * Number of threads to use for batch processing.
     * Default: Same as threads
     */
    var threadsBatch: Int = -1,

    // ========================================================================
    // Sampling Parameters
    // ========================================================================

    /**
     * Temperature for sampling.
     * Higher values (e.g., 1.0) make output more random.
     * Lower values (e.g., 0.2) make output more deterministic.
     * Default: 0.7
     */
    var temperature: Float = 0.7f,

    /**
     * Top-P (nucleus) sampling threshold.
     * Only tokens with cumulative probability <= topP are considered.
     * Default: 0.9
     */
    var topP: Float = 0.9f,

    /**
     * Top-K sampling.
     * Only the top K most likely tokens are considered.
     * Set to 0 to disable.
     * Default: 40
     */
    var topK: Int = 40,

    /**
     * Repetition penalty.
     * Values > 1.0 penalize repeated tokens.
     * Default: 1.1
     */
    var repeatPenalty: Float = 1.1f,

    /**
     * Frequency penalty.
     * Penalizes tokens based on frequency in the generated text.
     * Default: 0.0
     */
    var frequencyPenalty: Float = 0.0f,

    /**
     * Presence penalty.
     * Penalizes tokens that have appeared in the generated text.
     * Default: 0.0
     */
    var presencePenalty: Float = 0.0f,

    // ========================================================================
    // Generation Limits
    // ========================================================================

    /**
     * Maximum number of tokens to generate.
     * Default: 512
     */
    var maxTokens: Int = 512,

    /**
     * Stop sequences that will end generation.
     * Generation stops when any of these sequences is encountered.
     * Default: empty list
     */
    var stopSequences: List<String> = emptyList(),

    // ========================================================================
    // Memory Options
    // ========================================================================

    /**
     * Use memory-mapped file for model loading.
     * Reduces RAM usage but may be slower on some devices.
     * Default: true
     */
    var useMmap: Boolean = true,

    /**
     * Lock model in RAM to prevent swapping.
     * May improve performance but uses more memory.
     * Default: false
     */
    var useMlock: Boolean = false,

    // ========================================================================
    // GPU Options
    // ========================================================================

    /**
     * Number of layers to offload to GPU.
     * Set to 0 for CPU-only inference.
     * Note: GPU support depends on device capabilities.
     * Default: 0
     */
    var gpuLayers: Int = 0,

    // ========================================================================
    // Reproducibility
    // ========================================================================

    /**
     * Random seed for reproducible generation.
     * Set to -1 for random seed.
     * Default: -1
     */
    var seed: Int = -1
) {
    /**
     * Builder companion for DSL-style configuration.
     */
    companion object {
        /**
         * Create a LlamaConfig using DSL syntax.
         *
         * Example:
         * ```kotlin
         * val config = LlamaConfig {
         *     contextSize = 4096
         *     temperature = 0.8f
         * }
         * ```
         */
        inline operator fun invoke(block: LlamaConfig.() -> Unit): LlamaConfig {
            return LlamaConfig().apply(block)
        }

        /**
         * Default configuration for quick start.
         */
        val DEFAULT = LlamaConfig()

        /**
         * Configuration optimized for creative/storytelling tasks.
         */
        val CREATIVE = LlamaConfig(
            temperature = 0.9f,
            topP = 0.95f,
            topK = 50,
            repeatPenalty = 1.15f,
            maxTokens = 1024
        )

        /**
         * Configuration optimized for precise/factual tasks.
         */
        val PRECISE = LlamaConfig(
            temperature = 0.3f,
            topP = 0.8f,
            topK = 20,
            repeatPenalty = 1.05f,
            maxTokens = 512
        )

        /**
         * Configuration for deterministic output (same input = same output).
         */
        val DETERMINISTIC = LlamaConfig(
            temperature = 0.0f,
            topP = 1.0f,
            topK = 1,
            seed = 42
        )
    }

    /**
     * Validate configuration and throw [LlamaException.InvalidConfig] if invalid.
     */
    fun validate() {
        if (contextSize < 128) {
            throw LlamaException.InvalidConfig("contextSize must be at least 128")
        }
        if (contextSize > 131072) {
            throw LlamaException.InvalidConfig("contextSize must not exceed 131072")
        }
        if (batchSize < 1) {
            throw LlamaException.InvalidConfig("batchSize must be positive")
        }
        if (threads < 1) {
            throw LlamaException.InvalidConfig("threads must be at least 1")
        }
        if (temperature < 0.0f) {
            throw LlamaException.InvalidConfig("temperature must be non-negative")
        }
        if (topP < 0.0f || topP > 1.0f) {
            throw LlamaException.InvalidConfig("topP must be between 0.0 and 1.0")
        }
        if (topK < 0) {
            throw LlamaException.InvalidConfig("topK must be non-negative")
        }
        if (maxTokens < 1) {
            throw LlamaException.InvalidConfig("maxTokens must be at least 1")
        }
        if (gpuLayers < 0) {
            throw LlamaException.InvalidConfig("gpuLayers must be non-negative")
        }
    }

    /**
     * Create a copy with modified values using DSL syntax.
     *
     * Example:
     * ```kotlin
     * val newConfig = existingConfig.copy {
     *     temperature = 0.5f
     * }
     * ```
     */
    inline fun copy(block: LlamaConfig.() -> Unit): LlamaConfig {
        return this.copy().apply(block)
    }

    /**
     * Get effective batch thread count.
     */
    internal fun effectiveThreadsBatch(): Int {
        return if (threadsBatch > 0) threadsBatch else threads
    }
}
