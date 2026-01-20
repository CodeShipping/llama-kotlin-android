package org.codeshipping.llamakotlin.exception

/**
 * Sealed class hierarchy for LLaMA-related exceptions.
 * Provides type-safe exception handling for various error scenarios.
 */
sealed class LlamaException(
    message: String,
    cause: Throwable? = null
) : Exception(message, cause) {

    /**
     * Thrown when the model file is not found at the specified path.
     */
    class ModelNotFound(
        val path: String
    ) : LlamaException("Model file not found: $path")

    /**
     * Thrown when the model fails to load.
     * This can happen due to invalid model format, insufficient memory, etc.
     */
    class ModelLoadError(
        message: String,
        cause: Throwable? = null
    ) : LlamaException("Failed to load model: $message", cause)

    /**
     * Thrown when text generation fails.
     */
    class GenerationError(
        message: String,
        cause: Throwable? = null
    ) : LlamaException("Generation failed: $message", cause)

    /**
     * Thrown when the configuration is invalid.
     */
    class InvalidConfig(
        message: String
    ) : LlamaException("Invalid configuration: $message")

    /**
     * Thrown when a native (C++) error occurs.
     */
    class NativeError(
        val code: Int,
        message: String
    ) : LlamaException("Native error ($code): $message")

    /**
     * Thrown when attempting to use a model that hasn't been loaded.
     */
    class ModelNotLoaded : LlamaException("Model not loaded. Call load() first.")

    /**
     * Thrown when the model context has been closed/released.
     */
    class ContextClosed : LlamaException("Model context has been closed.")

    /**
     * Thrown when generation is cancelled.
     */
    class GenerationCancelled : LlamaException("Generation was cancelled.")

    /**
     * Thrown when the prompt exceeds the model's context size.
     */
    class PromptTooLong(
        val promptTokens: Int,
        val maxTokens: Int
    ) : LlamaException("Prompt too long: $promptTokens tokens exceeds maximum of $maxTokens tokens")
}
