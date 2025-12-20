#include "llama_context_wrapper.h"
#include <android/log.h>
#include <sstream>
#include <ctime>
#include <random>

#define LOG_TAG "LlamaAndroid"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace llamaandroid {

// Library version
static const char* LIBRARY_VERSION = "0.1.0";

LlamaContextWrapper::LlamaContextWrapper() {
    LOGI("LlamaContextWrapper created");
#if LLAMA_AVAILABLE
    // Initialize llama backend
    llama_backend_init();
    LOGI("llama.cpp backend initialized");
#else
    LOGW("llama.cpp not available - using stub implementation");
#endif
}

LlamaContextWrapper::~LlamaContextWrapper() {
    LOGI("LlamaContextWrapper destroying");
    unloadModel();
#if LLAMA_AVAILABLE
    llama_backend_free();
    LOGI("llama.cpp backend freed");
#endif
}

bool LlamaContextWrapper::loadModel(const std::string& modelPath, const LlamaConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    clearError();
    
    LOGI("Loading model from: %s", modelPath.c_str());
    
#if LLAMA_AVAILABLE
    // Unload existing model if any
    if (model_ != nullptr) {
        LOGI("Unloading existing model first");
        unloadModel();
    }
    
    // Set up model parameters
    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = config.gpuLayers;
    modelParams.use_mmap = config.useMmap;
    modelParams.use_mlock = config.useMlock;
    
    LOGI("Model params: gpu_layers=%d, use_mmap=%d, use_mlock=%d",
         config.gpuLayers, config.useMmap, config.useMlock);
    
    // Load the model using new API
    model_ = llama_model_load_from_file(modelPath.c_str(), modelParams);
    if (model_ == nullptr) {
        setError("Failed to load model from: " + modelPath);
        LOGE("%s", lastError_.c_str());
        return false;
    }
    
    LOGI("Model loaded successfully");
    
    // Set up context parameters
    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = config.contextSize;
    ctxParams.n_batch = config.batchSize;
    ctxParams.n_threads = config.threads;
    ctxParams.n_threads_batch = config.threadsBatch;
    
    LOGI("Context params: n_ctx=%d, n_batch=%d, n_threads=%d",
         ctxParams.n_ctx, ctxParams.n_batch, ctxParams.n_threads);
    
    // Create context using new API
    context_ = llama_init_from_model(model_, ctxParams);
    if (context_ == nullptr) {
        setError("Failed to create llama context");
        LOGE("%s", lastError_.c_str());
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }
    
    LOGI("Context created successfully");
    
    // Set up sampler with config seed
    setupSampler(config);
    
    currentConfig_ = config;
    LOGI("Model loading complete");
    return true;
    
#else
    // Stub implementation for testing without llama.cpp
    LOGW("Using stub implementation - model not actually loaded");
    currentConfig_ = config;
    return true;
#endif
}

void LlamaContextWrapper::unloadModel() {
    // Note: Don't lock mutex here as it may be called from destructor
    // or from loadModel which already holds the lock
    
    LOGI("Unloading model");
    
#if LLAMA_AVAILABLE
    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
        LOGD("Sampler freed");
    }
    
    if (context_ != nullptr) {
        llama_free(context_);
        context_ = nullptr;
        LOGD("Context freed");
    }
    
    if (model_ != nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
        LOGD("Model freed");
    }
#endif
    
    LOGI("Model unloaded");
}

bool LlamaContextWrapper::isModelLoaded() const {
#if LLAMA_AVAILABLE
    return model_ != nullptr && context_ != nullptr;
#else
    return true; // Stub always returns true for testing
#endif
}

std::string LlamaContextWrapper::generate(const std::string& prompt, const LlamaConfig* config) {
    std::string result;
    
    generateStream(prompt, [&result](const std::string& token) {
        result += token;
    }, config);
    
    return result;
}

void LlamaContextWrapper::generateStream(const std::string& prompt, TokenCallback callback, const LlamaConfig* config) {
    std::lock_guard<std::mutex> lock(mutex_);
    clearError();
    
    if (!isModelLoaded()) {
        setError("Model not loaded");
        LOGE("%s", lastError_.c_str());
        return;
    }
    
    const LlamaConfig& cfg = config ? *config : currentConfig_;
    
    LOGI("Starting generation for prompt length: %zu", prompt.length());
    LOGD("Prompt: %.100s...", prompt.c_str());
    
    isGenerating_ = true;
    shouldCancel_ = false;
    
#if LLAMA_AVAILABLE
    // Update sampler if config changed
    if (config != nullptr) {
        setupSampler(*config);
    }
    
    // Tokenize prompt
    std::vector<llama_token> promptTokens = tokenize(prompt, true);
    if (promptTokens.empty()) {
        setError("Failed to tokenize prompt");
        isGenerating_ = false;
        return;
    }
    
    LOGI("Tokenized prompt: %zu tokens", promptTokens.size());
    
    // Check context size
    const int n_ctx = llama_n_ctx(context_);
    if ((int)promptTokens.size() > n_ctx - 4) {
        setError("Prompt too long for context size");
        LOGE("Prompt tokens (%zu) exceeds context size (%d)", promptTokens.size(), n_ctx);
        isGenerating_ = false;
        return;
    }
    
    // Clear memory (KV cache) for new generation - new API uses llama_memory_clear
    llama_memory_t mem = llama_get_memory(context_);
    if (mem != nullptr) {
        llama_memory_clear(mem, true);
        LOGD("Memory cleared for new generation");
    }
    
    // Reset sampler state for new generation
    if (sampler_ != nullptr) {
        llama_sampler_reset(sampler_);
        LOGD("Sampler reset for new generation");
    }
    
    // Create batch for prompt processing
    llama_batch batch = llama_batch_init(cfg.batchSize, 0, 1);
    
    // Add prompt tokens to batch manually (llama_batch_add was in common.h helper)
    batch.n_tokens = 0;
    for (size_t i = 0; i < promptTokens.size(); i++) {
        batch.token[batch.n_tokens] = promptTokens[i];
        batch.pos[batch.n_tokens] = i;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = false;
        batch.n_tokens++;
    }
    
    // Set logits for last token
    if (batch.n_tokens > 0) {
        batch.logits[batch.n_tokens - 1] = true;
    }
    
    // Process prompt
    if (llama_decode(context_, batch) != 0) {
        setError("Failed to process prompt");
        llama_batch_free(batch);
        isGenerating_ = false;
        return;
    }
    
    LOGI("Prompt processed, starting generation");
    
    int n_cur = batch.n_tokens;
    int n_generated = 0;
    
    // Get vocab for token operations
    const llama_vocab * vocab = llama_model_get_vocab(model_);
    
    // Generation loop
    while (n_generated < cfg.maxTokens && !shouldCancel_) {
        // Sample next token
        llama_token newToken = llama_sampler_sample(sampler_, context_, -1);
        
        // Check for end of generation
        if (llama_vocab_is_eog(vocab, newToken)) {
            LOGI("End of generation token received");
            break;
        }
        
        // Convert token to text
        std::string tokenStr = detokenize({newToken});
        
        // Call callback with new token
        callback(tokenStr);
        
        // Prepare batch for next token
        batch.n_tokens = 0;
        batch.token[0] = newToken;
        batch.pos[0] = n_cur;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;
        batch.n_tokens = 1;
        
        // Decode
        if (llama_decode(context_, batch) != 0) {
            setError("Failed to decode token");
            break;
        }
        
        n_cur++;
        n_generated++;
    }
    
    llama_batch_free(batch);
    
    LOGI("Generation complete: %d tokens generated", n_generated);
    
#else
    // Stub implementation for testing
    LOGW("Using stub generation");
    
    std::string stubResponse = "Hello! This is a test response from llama-kotlin-android. ";
    stubResponse += "The library is working but llama.cpp is not compiled in. ";
    stubResponse += "Your prompt was: " + prompt.substr(0, 50) + "...";
    
    // Simulate streaming by sending word by word
    std::istringstream iss(stubResponse);
    std::string word;
    while (iss >> word && !shouldCancel_) {
        callback(word + " ");
    }
#endif
    
    isGenerating_ = false;
}

void LlamaContextWrapper::cancelGeneration() {
    LOGI("Generation cancellation requested");
    shouldCancel_ = true;
}

bool LlamaContextWrapper::isGenerating() const {
    return isGenerating_;
}

std::string LlamaContextWrapper::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

std::string LlamaContextWrapper::getVersion() {
#if LLAMA_AVAILABLE
    std::string version = LIBRARY_VERSION;
    version += " (llama.cpp)";
    return version;
#else
    return std::string(LIBRARY_VERSION) + " (stub)";
#endif
}

void LlamaContextWrapper::setError(const std::string& error) {
    lastError_ = error;
    LOGE("Error: %s", error.c_str());
}

void LlamaContextWrapper::clearError() {
    lastError_.clear();
}

#if LLAMA_AVAILABLE

std::vector<llama_token> LlamaContextWrapper::tokenize(const std::string& text, bool addBos) {
    // Get vocab from model
    const llama_vocab * vocab = llama_model_get_vocab(model_);
    
    // Estimate number of tokens (rough: 1 token per 4 chars)
    int n_tokens_estimate = text.length() / 4 + 16;
    std::vector<llama_token> tokens(n_tokens_estimate);
    
    // Tokenize using vocab
    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        text.length(),
        tokens.data(),
        tokens.size(),
        addBos,
        true  // parse special tokens
    );
    
    if (n_tokens < 0) {
        // Need more space
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            text.length(),
            tokens.data(),
            tokens.size(),
            addBos,
            true
        );
    }
    
    if (n_tokens < 0) {
        LOGE("Failed to tokenize text");
        return {};
    }
    
    tokens.resize(n_tokens);
    return tokens;
}

std::string LlamaContextWrapper::detokenize(const std::vector<llama_token>& tokens) {
    std::string result;
    
    // Get vocab from model
    const llama_vocab * vocab = llama_model_get_vocab(model_);
    
    for (llama_token token : tokens) {
        // Get token text
        char buf[256];
        int n = llama_token_to_piece(vocab, token, buf, sizeof(buf) - 1, 0, true);
        
        if (n < 0) {
            LOGW("Failed to detokenize token: %d", token);
            continue;
        }
        
        buf[n] = '\0';
        result += buf;
    }
    
    return result;
}

void LlamaContextWrapper::setupSampler(const LlamaConfig& config) {
    // Free existing sampler
    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
    }
    
    // Create sampler chain
    llama_sampler_chain_params chainParams = llama_sampler_chain_default_params();
    sampler_ = llama_sampler_chain_init(chainParams);
    
    // Add samplers in order
    
    // Repetition penalty (new signature: 4 args)
    if (config.repeatPenalty != 1.0f) {
        llama_sampler_chain_add(sampler_, 
            llama_sampler_init_penalties(
                64,                       // penalty_last_n
                config.repeatPenalty,    // penalty_repeat
                0.0f,                    // penalty_freq
                0.0f                     // penalty_present
            )
        );
    }
    
    // Top-K sampling
    if (config.topK > 0) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(config.topK));
    }
    
    // Top-P (nucleus) sampling
    if (config.topP < 1.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(config.topP, 1));
    }
    
    // Temperature
    if (config.temperature > 0.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(config.temperature));
    }
    
    // Distribution sampling with seed
    uint32_t seed = config.seed >= 0 ? config.seed : static_cast<uint32_t>(std::time(nullptr));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(seed));
    
    LOGI("Sampler configured: temp=%.2f, top_p=%.2f, top_k=%d, repeat_penalty=%.2f",
         config.temperature, config.topP, config.topK, config.repeatPenalty);
}

#endif // LLAMA_AVAILABLE

} // namespace llamaandroid
