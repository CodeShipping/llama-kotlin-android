#include <jni.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <android/log.h>

#include "llama_context_wrapper.h"

#define LOG_TAG "LlamaJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

using namespace llamaandroid;

// Global context manager
static std::unordered_map<jlong, std::unique_ptr<LlamaContextWrapper>> g_contexts;
static std::mutex g_contextsMutex;
static jlong g_nextContextId = 1;

// Helper to get context from handle
static LlamaContextWrapper* getContext(jlong handle) {
    std::lock_guard<std::mutex> lock(g_contextsMutex);
    auto it = g_contexts.find(handle);
    if (it != g_contexts.end()) {
        return it->second.get();
    }
    return nullptr;
}

// Helper to convert jstring to std::string
static std::string jstringToString(JNIEnv* env, jstring jstr) {
    if (jstr == nullptr) {
        return "";
    }
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

// Helper to convert std::string to jstring
static jstring stringToJstring(JNIEnv* env, const std::string& str) {
    return env->NewStringUTF(str.c_str());
}

// Helper to throw Java exception - uses RuntimeException for safety
static void throwException(JNIEnv* env, const char* className, const char* message) {
    // Try the specified class first
    jclass exClass = env->FindClass(className);
    if (exClass != nullptr && !env->ExceptionCheck()) {
        env->ThrowNew(exClass, message);
        env->DeleteLocalRef(exClass);
    } else {
        // Fall back to RuntimeException if class not found
        env->ExceptionClear();
        jclass runtimeEx = env->FindClass("java/lang/RuntimeException");
        if (runtimeEx != nullptr) {
            env->ThrowNew(runtimeEx, message);
            env->DeleteLocalRef(runtimeEx);
        }
    }
}

// Helper to throw LlamaException.GenerationError
static void throwGenerationError(JNIEnv* env, const char* message) {
    jclass exClass = env->FindClass("org/codeshipping/llamakotlin/exception/LlamaException$GenerationError");
    if (exClass != nullptr && !env->ExceptionCheck()) {
        // Find constructor
        jmethodID constructor = env->GetMethodID(exClass, "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V");
        if (constructor != nullptr) {
            jstring jmsg = env->NewStringUTF(message);
            jobject ex = env->NewObject(exClass, constructor, jmsg, nullptr);
            if (ex != nullptr) {
                env->Throw((jthrowable)ex);
                env->DeleteLocalRef(ex);
            }
            env->DeleteLocalRef(jmsg);
        } else {
            // Fallback to RuntimeException
            env->ExceptionClear();
            throwException(env, "java/lang/RuntimeException", message);
        }
        env->DeleteLocalRef(exClass);
    } else {
        env->ExceptionClear();
        throwException(env, "java/lang/RuntimeException", message);
    }
}

// Convert Java LlamaConfig to native LlamaConfig
static LlamaConfig configFromJava(JNIEnv* env, jobject jconfig) {
    LlamaConfig config;
    
    if (jconfig == nullptr) {
        return config;
    }
    
    jclass configClass = env->GetObjectClass(jconfig);
    
    // Get field IDs
    jfieldID contextSizeField = env->GetFieldID(configClass, "contextSize", "I");
    jfieldID batchSizeField = env->GetFieldID(configClass, "batchSize", "I");
    jfieldID threadsField = env->GetFieldID(configClass, "threads", "I");
    jfieldID temperatureField = env->GetFieldID(configClass, "temperature", "F");
    jfieldID topPField = env->GetFieldID(configClass, "topP", "F");
    jfieldID topKField = env->GetFieldID(configClass, "topK", "I");
    jfieldID repeatPenaltyField = env->GetFieldID(configClass, "repeatPenalty", "F");
    jfieldID maxTokensField = env->GetFieldID(configClass, "maxTokens", "I");
    jfieldID useMmapField = env->GetFieldID(configClass, "useMmap", "Z");
    jfieldID useMlockField = env->GetFieldID(configClass, "useMlock", "Z");
    jfieldID gpuLayersField = env->GetFieldID(configClass, "gpuLayers", "I");
    jfieldID seedField = env->GetFieldID(configClass, "seed", "I");
    
    // Read values
    if (contextSizeField) config.contextSize = env->GetIntField(jconfig, contextSizeField);
    if (batchSizeField) config.batchSize = env->GetIntField(jconfig, batchSizeField);
    if (threadsField) config.threads = env->GetIntField(jconfig, threadsField);
    if (temperatureField) config.temperature = env->GetFloatField(jconfig, temperatureField);
    if (topPField) config.topP = env->GetFloatField(jconfig, topPField);
    if (topKField) config.topK = env->GetIntField(jconfig, topKField);
    if (repeatPenaltyField) config.repeatPenalty = env->GetFloatField(jconfig, repeatPenaltyField);
    if (maxTokensField) config.maxTokens = env->GetIntField(jconfig, maxTokensField);
    if (useMmapField) config.useMmap = env->GetBooleanField(jconfig, useMmapField);
    if (useMlockField) config.useMlock = env->GetBooleanField(jconfig, useMlockField);
    if (gpuLayersField) config.gpuLayers = env->GetIntField(jconfig, gpuLayersField);
    if (seedField) config.seed = env->GetIntField(jconfig, seedField);
    
    env->DeleteLocalRef(configClass);
    
    return config;
}

extern "C" {

// ============================================================================
// Native Library Management
// ============================================================================

JNIEXPORT jstring JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeGetVersion(
    JNIEnv* env,
    jclass /* clazz */) {
    return stringToJstring(env, LlamaContextWrapper::getVersion());
}

// ============================================================================
// Context Management
// ============================================================================

JNIEXPORT jlong JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeCreateContext(
    JNIEnv* env,
    jclass /* clazz */) {
    LOGI("Creating new LlamaContext");
    
    std::lock_guard<std::mutex> lock(g_contextsMutex);
    
    auto context = std::make_unique<LlamaContextWrapper>();
    jlong handle = g_nextContextId++;
    
    g_contexts[handle] = std::move(context);
    
    LOGI("Created context with handle: %lld", (long long)handle);
    return handle;
}

JNIEXPORT void JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeDestroyContext(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    LOGI("Destroying context: %lld", (long long)handle);
    
    std::lock_guard<std::mutex> lock(g_contextsMutex);
    
    auto it = g_contexts.find(handle);
    if (it != g_contexts.end()) {
        g_contexts.erase(it);
        LOGI("Context destroyed: %lld", (long long)handle);
    } else {
        LOGW("Context not found for destruction: %lld", (long long)handle);
    }
}

// ============================================================================
// Model Loading
// ============================================================================

JNIEXPORT jboolean JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeLoadModel(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle,
    jstring modelPath,
    jobject jconfig) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        throwException(env, "java/lang/IllegalStateException", "Invalid context handle");
        return JNI_FALSE;
    }
    
    std::string path = jstringToString(env, modelPath);
    LlamaConfig config = configFromJava(env, jconfig);
    
    LOGI("Loading model: %s", path.c_str());
    
    bool success = context->loadModel(path, config);
    
    if (!success) {
        std::string error = context->getLastError();
        throwGenerationError(env, error.c_str());
        return JNI_FALSE;
    }
    
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeUnloadModel(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        LOGW("Invalid context handle for unloadModel");
        return;
    }
    
    context->unloadModel();
}

JNIEXPORT jboolean JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeIsModelLoaded(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        return JNI_FALSE;
    }
    
    return context->isModelLoaded() ? JNI_TRUE : JNI_FALSE;
}

// ============================================================================
// Text Generation
// ============================================================================

JNIEXPORT jstring JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeGenerate(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle,
    jstring prompt,
    jobject jconfig) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        throwException(env, "java/lang/IllegalStateException", "Invalid context handle");
        return nullptr;
    }
    
    std::string promptStr = jstringToString(env, prompt);
    
    LlamaConfig config;
    LlamaConfig* configPtr = nullptr;
    if (jconfig != nullptr) {
        config = configFromJava(env, jconfig);
        configPtr = &config;
    }
    
    std::string result = context->generate(promptStr, configPtr);
    
    if (result.empty() && !context->getLastError().empty()) {
        throwGenerationError(env, context->getLastError().c_str());
        return nullptr;
    }
    
    return stringToJstring(env, result);
}

// Callback structure for streaming
struct StreamCallbackData {
    JNIEnv* env;
    jobject callback;
    jmethodID onTokenMethod;
    bool hasError;
    std::string errorMessage;
};

JNIEXPORT void JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeGenerateStream(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle,
    jstring prompt,
    jobject callback,
    jobject jconfig) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        throwException(env, "java/lang/IllegalStateException", "Invalid context handle");
        return;
    }
    
    if (callback == nullptr) {
        throwException(env, "java/lang/IllegalArgumentException", "Callback cannot be null");
        return;
    }
    
    std::string promptStr = jstringToString(env, prompt);
    
    LlamaConfig config;
    LlamaConfig* configPtr = nullptr;
    if (jconfig != nullptr) {
        config = configFromJava(env, jconfig);
        configPtr = &config;
    }
    
    // Get callback method
    jclass callbackClass = env->GetObjectClass(callback);
    jmethodID onTokenMethod = env->GetMethodID(callbackClass, "onToken", "(Ljava/lang/String;)V");
    
    if (onTokenMethod == nullptr) {
        env->DeleteLocalRef(callbackClass);
        throwException(env, "java/lang/NoSuchMethodException", "Callback must have onToken(String) method");
        return;
    }
    
    // Create global ref for callback
    jobject globalCallback = env->NewGlobalRef(callback);
    bool hasCallbackError = false;
    
    // Stream generation with callback
    context->generateStream(promptStr, [env, globalCallback, onTokenMethod, &hasCallbackError](const std::string& token) {
        // Skip if we already had an error
        if (hasCallbackError) {
            return;
        }
        
        // Note: This callback is called from the same thread, so we can use env directly
        jstring jtoken = env->NewStringUTF(token.c_str());
        if (jtoken == nullptr) {
            LOGE("Failed to create jstring for token");
            hasCallbackError = true;
            return;
        }
        
        env->CallVoidMethod(globalCallback, onTokenMethod, jtoken);
        env->DeleteLocalRef(jtoken);
        
        // Check for exceptions
        if (env->ExceptionCheck()) {
            LOGE("Exception in token callback");
            hasCallbackError = true;
            env->ExceptionClear(); // Clear to allow cleanup
        }
    }, configPtr);
    
    // Clean up global ref first
    env->DeleteGlobalRef(globalCallback);
    env->DeleteLocalRef(callbackClass);
    
    // Check for errors - don't throw if already completed successfully
    std::string error = context->getLastError();
    if (!error.empty() && !hasCallbackError) {
        LOGE("Generation error: %s", error.c_str());
        throwGenerationError(env, error.c_str());
    }
}

// ============================================================================
// Generation Control
// ============================================================================

JNIEXPORT void JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeCancelGeneration(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context != nullptr) {
        context->cancelGeneration();
    }
}

JNIEXPORT jboolean JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeIsGenerating(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        return JNI_FALSE;
    }
    
    return context->isGenerating() ? JNI_TRUE : JNI_FALSE;
}

// ============================================================================
// Error Handling
// ============================================================================

JNIEXPORT jstring JNICALL
Java_org_codeshipping_llamakotlin_LlamaNative_nativeGetLastError(
    JNIEnv* env,
    jclass /* clazz */,
    jlong handle) {
    
    LlamaContextWrapper* context = getContext(handle);
    if (context == nullptr) {
        return stringToJstring(env, "Invalid context handle");
    }
    
    return stringToJstring(env, context->getLastError());
}

} // extern "C"
