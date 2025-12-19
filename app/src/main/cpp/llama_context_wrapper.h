#ifndef LLAMA_CONTEXT_WRAPPER_H
#define LLAMA_CONTEXT_WRAPPER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>

#if LLAMA_AVAILABLE
#include "llama.h"
#endif

namespace llamaandroid {

/**
 * Configuration for LLaMA model loading and inference
 */
struct LlamaConfig {
    // Context parameters
    int contextSize = 2048;
    int batchSize = 512;
    
    // Threading
    int threads = 4;
    int threadsBatch = 4;
    
    // Sampling parameters
    float temperature = 0.7f;
    float topP = 0.9f;
    int topK = 40;
    float repeatPenalty = 1.1f;
    
    // Generation limits
    int maxTokens = 512;
    
    // Memory options
    bool useMmap = true;
    bool useMlock = false;
    
    // GPU layers (0 = CPU only)
    int gpuLayers = 0;
    
    // Seed for reproducibility (-1 = random)
    int seed = -1;
};

/**
 * Token callback function type for streaming
 */
using TokenCallback = std::function<void(const std::string& token)>;

/**
 * Wrapper class for llama.cpp context management
 */
class LlamaContextWrapper {
public:
    LlamaContextWrapper();
    ~LlamaContextWrapper();
    
    // Prevent copying
    LlamaContextWrapper(const LlamaContextWrapper&) = delete;
    LlamaContextWrapper& operator=(const LlamaContextWrapper&) = delete;
    
    /**
     * Load a GGUF model from the specified path
     * @param modelPath Path to the .gguf model file
     * @param config Configuration for model loading
     * @return true if successful, false otherwise
     */
    bool loadModel(const std::string& modelPath, const LlamaConfig& config);
    
    /**
     * Unload the current model and free resources
     */
    void unloadModel();
    
    /**
     * Check if a model is currently loaded
     */
    bool isModelLoaded() const;
    
    /**
     * Generate a complete response for the given prompt
     * @param prompt Input text prompt
     * @param config Sampling configuration (optional, uses default if not provided)
     * @return Generated text response
     */
    std::string generate(const std::string& prompt, const LlamaConfig* config = nullptr);
    
    /**
     * Generate a streaming response, calling the callback for each token
     * @param prompt Input text prompt
     * @param callback Function to call for each generated token
     * @param config Sampling configuration (optional)
     */
    void generateStream(const std::string& prompt, TokenCallback callback, const LlamaConfig* config = nullptr);
    
    /**
     * Cancel ongoing generation
     */
    void cancelGeneration();
    
    /**
     * Check if generation is currently in progress
     */
    bool isGenerating() const;
    
    /**
     * Get the last error message
     */
    std::string getLastError() const;
    
    /**
     * Get library version string
     */
    static std::string getVersion();
    
private:
#if LLAMA_AVAILABLE
    llama_model* model_ = nullptr;
    llama_context* context_ = nullptr;
    llama_sampler* sampler_ = nullptr;
#endif
    
    LlamaConfig currentConfig_;
    std::string lastError_;
    std::atomic<bool> isGenerating_{false};
    std::atomic<bool> shouldCancel_{false};
    mutable std::mutex mutex_;
    
    void setError(const std::string& error);
    void clearError();
    
#if LLAMA_AVAILABLE
    std::vector<llama_token> tokenize(const std::string& text, bool addBos);
    std::string detokenize(const std::vector<llama_token>& tokens);
    void setupSampler(const LlamaConfig& config);
#endif
};

} // namespace llamaandroid

#endif // LLAMA_CONTEXT_WRAPPER_H
