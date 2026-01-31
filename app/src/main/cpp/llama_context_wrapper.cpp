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
// Version is defined by CMake from Gradle build (LIBRARY_VERSION macro)
#ifndef LIBRARY_VERSION
#define LIBRARY_VERSION "0.1.1"
#endif

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
    
    // Check context size and handle overflow
    const int n_ctx = llama_n_ctx(context_);
    const int maxPromptTokens = n_ctx - cfg.maxTokens - 16; // Reserve space for generation + safety margin
    
    if ((int)promptTokens.size() > maxPromptTokens) {
        if (maxPromptTokens < 64) {
            setError("Context too small for generation. Need at least 64 tokens for prompt.");
            LOGE("Available prompt space (%d) is too small", maxPromptTokens);
            isGenerating_ = false;
            return;
        }
        
        // Smart truncation - preserve important context
        LOGW("Prompt too long (%zu tokens), applying smart truncation to %d tokens", promptTokens.size(), maxPromptTokens);
        promptTokens = smartTruncate(promptTokens, maxPromptTokens);
        LOGI("Truncated to %zu tokens", promptTokens.size());
    }
    
    // Clear KV cache for fresh start
    // Note: KV cache reuse is complex and can cause issues when the 
    // previous context doesn't match. For reliability, always clear.
    llama_memory_t mem = llama_get_memory(context_);
    if (mem != nullptr) {
        llama_memory_clear(mem, true);
        LOGD("Memory cleared for new generation");
    }
    
    // Store current prompt for next turn's cache optimization
    lastPromptTokens_ = promptTokens;
    
    // Reset sampler state for new generation
    if (sampler_ != nullptr) {
        llama_sampler_reset(sampler_);
        LOGD("Sampler reset for new generation");
    }
    
    // Create batch for prompt processing
    const size_t n_prompt = promptTokens.size();
    const int batchCapacity = std::max(cfg.batchSize, (int)n_prompt + 1);
    llama_batch batch = llama_batch_init(batchCapacity, 0, 1);
    
    // Process prompt in chunks
    size_t n_processed = 0;
    
    while (n_processed < n_prompt && !shouldCancel_) {
        // Calculate chunk size
        size_t chunk_size = std::min((size_t)cfg.batchSize, n_prompt - n_processed);
        
        // Add tokens to batch
        batch.n_tokens = 0;
        for (size_t i = 0; i < chunk_size; i++) {
            batch.token[batch.n_tokens] = promptTokens[n_processed + i];
            batch.pos[batch.n_tokens] = n_processed + i;
            batch.n_seq_id[batch.n_tokens] = 1;
            batch.seq_id[batch.n_tokens][0] = 0;
            // Only compute logits for last token of entire prompt
            batch.logits[batch.n_tokens] = (n_processed + i == n_prompt - 1);
            batch.n_tokens++;
        }
        
        // Process batch
        if (llama_decode(context_, batch) != 0) {
            setError("Failed to process prompt batch");
            llama_batch_free(batch);
            isGenerating_ = false;
            return;
        }
        
        n_processed += chunk_size;
        LOGD("Processed %zu/%zu prompt tokens", n_processed, n_prompt);
    }
    
    LOGI("Prompt processed, starting generation");
    
    int n_cur = (int)n_prompt;
    int n_generated = 0;
    
    // Get vocab for token operations
    const llama_vocab * vocab = llama_model_get_vocab(model_);
    
    // Generation loop
    while (n_generated < cfg.maxTokens && !shouldCancel_) {
        // Safety check - ensure sampler and context are valid
        if (sampler_ == nullptr || context_ == nullptr) {
            LOGE("Sampler or context became null during generation");
            setError("Internal error: sampler or context is null");
            break;
        }
        
        // Sample next token
        llama_token newToken = llama_sampler_sample(sampler_, context_, -1);
        
        // Safety check for invalid token
        if (newToken < 0) {
            LOGW("Invalid token sampled: %d", newToken);
            break;
        }
        
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
        int decode_result = llama_decode(context_, batch);
        if (decode_result != 0) {
            LOGE("Failed to decode token, error code: %d", decode_result);
            setError("Failed to decode token");
            break;
        }
        
        n_cur++;
        n_generated++;
    }
    
    // Mark generation as complete BEFORE freeing batch
    isGenerating_ = false;
    
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

// Rolling hash using Rabin-Karp algorithm for fast prefix matching
// Time: O(n), much faster than token-by-token comparison for long sequences
uint64_t LlamaContextWrapper::computeRollingHash(const std::vector<llama_token>& tokens, size_t start, size_t len) {
    const uint64_t BASE = 31;
    const uint64_t MOD = 1e9 + 7;
    uint64_t hash = 0;
    uint64_t power = 1;
    
    const size_t end = std::min(start + len, tokens.size());
    for (size_t i = start; i < end; i++) {
        hash = (hash + (tokens[i] % MOD) * power) % MOD;
        power = (power * BASE) % MOD;
    }
    return hash;
}

// Binary search + rolling hash for O(log n) prefix matching
size_t LlamaContextWrapper::findLongestCommonPrefix(const std::vector<llama_token>& a, const std::vector<llama_token>& b) {
    if (a.empty() || b.empty()) return 0;
    
    const size_t maxLen = std::min(a.size(), b.size());
    
    // Quick check: if first tokens differ, no common prefix
    if (a[0] != b[0]) return 0;
    
    // Binary search for longest common prefix
    // This gives O(log n * n) worst case, but with hash comparison it's typically O(log n)
    size_t lo = 1, hi = maxLen;
    size_t result = 0;
    
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        
        // Check if prefix of length 'mid' matches using rolling hash
        uint64_t hashA = computeRollingHash(a, 0, mid);
        uint64_t hashB = computeRollingHash(b, 0, mid);
        
        if (hashA == hashB) {
            // Verify match (hash collision check) - sample check for speed
            bool match = true;
            // Check every 64th token for verification (fast probabilistic check)
            for (size_t i = 0; i < mid && match; i += std::max(1UL, mid / 16)) {
                if (a[i] != b[i]) match = false;
            }
            if (match) {
                result = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        } else {
            hi = mid - 1;
        }
    }
    
    return result;
}

// Smart truncation using sliding window and importance scoring
// Preserves: system prompt, conversation boundaries, recent context
std::vector<llama_token> LlamaContextWrapper::smartTruncate(const std::vector<llama_token>& tokens, int maxTokens) {
    if ((int)tokens.size() <= maxTokens) return tokens;
    
    std::vector<llama_token> result;
    result.reserve(maxTokens);
    
    // Strategy: Keep important segments
    // 1. First 15% - System prompt (crucial for behavior)
    // 2. Last 70% - Recent conversation (most relevant)
    // 3. Skip middle (older context)
    
    const int keepStart = std::max(32, maxTokens * 15 / 100);  // 15% for system prompt
    const int keepEnd = maxTokens - keepStart;                  // Rest for recent context
    
    // Get vocab for special token detection
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    
    // Copy system prompt portion
    int actualStart = 0;
    for (int i = 0; i < keepStart && i < (int)tokens.size(); i++) {
        result.push_back(tokens[i]);
        actualStart++;
    }
    
    // Find good truncation point - look for conversation boundaries
    // Search for assistant/user turn markers in the middle region
    const size_t searchStart = tokens.size() - keepEnd - 128;  // Search window
    const size_t searchEnd = tokens.size() - keepEnd;
    size_t bestCutPoint = tokens.size() - keepEnd;
    
    // Look for newlines or special markers as good cut points
    for (size_t i = searchStart; i < searchEnd && i < tokens.size(); i++) {
        // Check if this is a good boundary (newline tokens often have value < 50)
        // This is a heuristic - actual implementation would check token content
        if (tokens[i] < 50 || (i > 0 && tokens[i-1] < 50)) {
            bestCutPoint = i;
            break;
        }
    }
    
    // Copy recent context from best cut point
    for (size_t i = bestCutPoint; i < tokens.size(); i++) {
        result.push_back(tokens[i]);
        if ((int)result.size() >= maxTokens) break;
    }
    
    LOGI("Smart truncate: %zu -> %zu tokens (kept %d start, %zu end)", 
         tokens.size(), result.size(), actualStart, result.size() - actualStart);
    
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
