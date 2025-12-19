# 🦙 LLaMA Kotlin Android

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![API](https://img.shields.io/badge/API-24%2B-brightgreen.svg)](https://android-arsenal.com/api?level=24)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.0.21-purple.svg)](https://kotlinlang.org)

A Kotlin-first Android library for running LLaMA models on-device using [llama.cpp](https://github.com/ggerganov/llama.cpp). Lightweight, easy-to-use API with full coroutine support following modern Android best practices.

## Features

- **On-device inference** - No internet required, complete privacy
- **Kotlin-first API** - Idiomatic, DSL-style configuration
- **Full Coroutine Support** - `Flow<String>` for streaming, structured concurrency
- **Multiple quantization** - Q4_0, Q4_K_M, Q5_K_M, Q8_0 support
- **Small footprint** - ~15 MB library size (without models)
- **Easy integration** - Single Gradle dependency
- **Memory safe** - Automatic resource cleanup with Closeable pattern

---

## Table of Contents

- [Architecture](#-architecture)
- [Project Structure](#-project-structure)
- [Implementation Roadmap](#-implementation-roadmap)
- [Installation](#-installation)
- [Quick Start](#-quick-start)
- [API Reference](#-api-reference)
- [Supported Models](#-supported-models)
- [Requirements](#-requirements)
- [Contributing](#-contributing)
- [License](#-license)

---

## Architecture

This library follows **Clean Architecture** principles with a clear separation between native (C++) and Kotlin layers.

```
┌─────────────────────────────────────────────────────────────────┐
│                      Application Layer                          │
│  (Your App using the library via suspend functions & Flows)     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      API Layer (Kotlin)                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ LlamaModel  │  │ LlamaConfig │  │ LlamaException          │ │
│  │ (suspend)   │  │ (DSL)       │  │ (sealed class)          │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │           Coroutine Dispatchers Management                  ││
│  │  - IO Dispatcher for model loading                          ││
│  │  - Default Dispatcher for inference                         ││
│  │  - Callback bridging to Flow                                ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      JNI Bridge Layer                           │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                   LlamaNative.kt                            ││
│  │  - external fun loadModel(path: String): Long               ││
│  │  - external fun generate(ctx: Long, prompt: String): String ││
│  │  - external fun generateStream(ctx: Long, prompt: String,   ││
│  │                                callback: (String) -> Unit)  ││
│  │  - external fun freeModel(ctx: Long)                        ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Native Layer (C++)                         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    llama_jni.cpp                            ││
│  │  - JNI function implementations                             ││
│  │  - Thread-safe context management                           ││
│  │  - Callback bridging to Java/Kotlin                         ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    llama.cpp                                ││
│  │  - GGUF model loading                                       ││
│  │  - Quantized inference                                      ││
│  │  - ARM NEON / x86 optimizations                             ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### Coroutine Integration Design

```kotlin
// All public APIs are suspend functions or return Flow
class LlamaModel {
    // Suspend function for one-shot generation
    suspend fun generate(prompt: String): String
    
    // Flow for streaming token-by-token
    fun generateStream(prompt: String): Flow<String>
    
    // Suspend function for model loading
    companion object {
        suspend fun load(path: String, config: LlamaConfig = LlamaConfig()): LlamaModel
    }
}
```

---

## Project Structure

```
llama-kotlin-android/
│
├── llama-android/                      # 📦 Main library module
│   ├── build.gradle.kts
│   ├── consumer-rules.pro
│   ├── proguard-rules.pro
│   │
│   └── src/main/
│       ├── AndroidManifest.xml
│       │
│       ├── cpp/                        # 🔧 Native C++ code
│       │   ├── CMakeLists.txt          # CMake build configuration
│       │   ├── llama_jni.cpp           # JNI bridge implementation
│       │   ├── llama_context.h         # Context wrapper header
│       │   ├── llama_context.cpp       # Context wrapper implementation
│       │   └── llama.cpp/              # Git submodule: llama.cpp source
│       │       ├── llama.cpp
│       │       ├── llama.h
│       │       ├── ggml.c
│       │       ├── ggml.h
│       │       └── ...
│       │
│       └── kotlin/com/llamakotlin/android/
│           │
│           ├── LlamaModel.kt           # Main entry point (suspend functions)
│           ├── LlamaConfig.kt          # Configuration DSL
│           ├── LlamaNative.kt          # JNI declarations (internal)
│           │
│           ├── exception/              # Exception handling
│           │   └── LlamaException.kt   # Sealed exception hierarchy
│           │
│           ├── internal/               # Internal utilities
│           │   ├── NativeLoader.kt     # Native library loading
│           │   └── ContextManager.kt   # Thread-safe context management
│           │
│           └── extension/              # Kotlin extensions
│               └── FlowExtensions.kt   # Flow utilities
│
├── sample-app/                         # 📱 Demo application
│   ├── build.gradle.kts
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── kotlin/com/llamakotlin/sample/
│       │   ├── MainActivity.kt
│       │   ├── MainViewModel.kt        # ViewModel with coroutine scope
│       │   └── ui/
│       │       └── ChatScreen.kt       # Jetpack Compose UI
│       └── res/
│
├── build.gradle.kts                    # Root build file
├── settings.gradle.kts                 # Module settings
├── gradle.properties                   # Gradle properties
├── gradle/
│   └── libs.versions.toml              # Version catalog
│
├── LICENSE
└── README.md
```

---

## Implementation Roadmap

### Phase 1: Project Setup
- [x] Create Android project with Kotlin DSL
- [x] Configure version catalog
- [x] Set up Git repository

### Phase 2: Module Structure
- [ ] Convert `app` module to library module (`llama-android`)
- [ ] Create sample app module (`sample-app`)
- [ ] Configure NDK and CMake

### Phase 3: Native Layer (C++)
- [ ] Add llama.cpp as git submodule
- [ ] Create `CMakeLists.txt` for Android NDK build
- [ ] Implement `llama_jni.cpp` with JNI functions
- [ ] Configure ABI filters (arm64-v8a, armeabi-v7a, x86_64)

### Phase 4: Kotlin API Layer
- [ ] Implement `LlamaNative.kt` - JNI declarations
- [ ] Implement `LlamaConfig.kt` - Configuration DSL
- [ ] Implement `LlamaException.kt` - Sealed exception hierarchy
- [ ] Implement `LlamaModel.kt` - Main API with coroutines

### Phase 5: Coroutine Integration
- [ ] Wrap native calls with `withContext(Dispatchers.IO)`
- [ ] Implement streaming with `callbackFlow`
- [ ] Add cancellation support
- [ ] Implement proper resource cleanup

### Phase 6: Sample App
- [ ] Create ViewModel with coroutine scope
- [ ] Implement Jetpack Compose UI
- [ ] Add model download functionality
- [ ] Demonstrate streaming chat

### Phase 7: Testing & Documentation
- [ ] Unit tests for Kotlin layer
- [ ] Integration tests with sample model
- [ ] KDoc documentation
- [ ] Usage examples

### Phase 8: Publishing
- [ ] Configure Maven publishing
- [ ] Set up GitHub Actions for CI/CD
- [ ] Publish to Maven Central

---

## 📥 Installation

### Gradle (Kotlin DSL)

```kotlin
dependencies {
    implementation("io.github.it5prasoon:llama-kotlin-android:1.0.0")
}
```

### Gradle (Groovy)

```groovy
dependencies {
    implementation 'io.github.it5prasoon:llama-kotlin-android:1.0.0'
}
```

---

## 🚀 Quick Start

### Basic Usage

```kotlin
import com.llamakotlin.android.LlamaModel
import com.llamakotlin.android.LlamaConfig
import kotlinx.coroutines.flow.collect

class MyViewModel : ViewModel() {
    
    private var llamaModel: LlamaModel? = null
    
    fun loadModel(modelPath: String) {
        viewModelScope.launch {
            try {
                // Load model with custom configuration
                llamaModel = LlamaModel.load(modelPath) {
                    contextSize = 2048
                    threads = 4
                    temperature = 0.7f
                    topP = 0.9f
                }
            } catch (e: LlamaException) {
                // Handle error
            }
        }
    }
    
    // One-shot generation
    fun generateResponse(prompt: String) {
        viewModelScope.launch {
            val response = llamaModel?.generate(prompt)
            // Use response
        }
    }
    
    // Streaming generation
    fun generateStream(prompt: String) {
        viewModelScope.launch {
            llamaModel?.generateStream(prompt)
                ?.collect { token ->
                    // Handle each token as it arrives
                    appendToChat(token)
                }
        }
    }
    
    override fun onCleared() {
        super.onCleared()
        llamaModel?.close() // Clean up native resources
    }
}
```

### DSL Configuration

```kotlin
val model = LlamaModel.load(modelPath) {
    // Context configuration
    contextSize = 4096          // Max context length
    batchSize = 512             // Batch size for prompt processing
    
    // Threading
    threads = 4                 // Number of threads for inference
    
    // Sampling parameters
    temperature = 0.8f          // Randomness (0.0 - 2.0)
    topP = 0.95f               // Nucleus sampling
    topK = 40                   // Top-K sampling
    repeatPenalty = 1.1f       // Repetition penalty
    
    // Generation limits
    maxTokens = 512            // Max tokens to generate
    
    // Memory options
    useMmap = true             // Memory-map model file
    useMlock = false           // Lock model in RAM
    
    // GPU options (if available)
    gpuLayers = 0              // Number of layers to offload to GPU
}
```

### With Jetpack Compose

```kotlin
@Composable
fun ChatScreen(viewModel: ChatViewModel = viewModel()) {
    val messages by viewModel.messages.collectAsState()
    val isGenerating by viewModel.isGenerating.collectAsState()
    
    Column {
        LazyColumn(modifier = Modifier.weight(1f)) {
            items(messages) { message ->
                ChatBubble(message)
            }
        }
        
        ChatInput(
            enabled = !isGenerating,
            onSend = { prompt ->
                viewModel.sendMessage(prompt)
            }
        )
    }
}

@HiltViewModel
class ChatViewModel @Inject constructor(
    private val llamaModel: LlamaModel
) : ViewModel() {
    
    private val _messages = MutableStateFlow<List<Message>>(emptyList())
    val messages: StateFlow<List<Message>> = _messages.asStateFlow()
    
    private val _isGenerating = MutableStateFlow(false)
    val isGenerating: StateFlow<Boolean> = _isGenerating.asStateFlow()
    
    fun sendMessage(prompt: String) {
        viewModelScope.launch {
            _isGenerating.value = true
            _messages.update { it + Message.User(prompt) }
            
            val responseBuilder = StringBuilder()
            _messages.update { it + Message.Assistant("") }
            
            llamaModel.generateStream(prompt)
                .catch { e -> 
                    _messages.update { messages ->
                        messages.dropLast(1) + Message.Error(e.message ?: "Error")
                    }
                }
                .collect { token ->
                    responseBuilder.append(token)
                    _messages.update { messages ->
                        messages.dropLast(1) + Message.Assistant(responseBuilder.toString())
                    }
                }
            
            _isGenerating.value = false
        }
    }
}
```

---

## 📚 API Reference

### LlamaModel

```kotlin
class LlamaModel : Closeable {
    
    companion object {
        /**
         * Load a GGUF model from the specified path.
         * 
         * @param modelPath Absolute path to the .gguf model file
         * @param config Configuration block using DSL
         * @return Loaded LlamaModel instance
         * @throws LlamaException.ModelLoadError if model fails to load
         */
        suspend fun load(
            modelPath: String, 
            config: LlamaConfig.() -> Unit = {}
        ): LlamaModel
        
        /**
         * Get library version string.
         */
        fun getVersion(): String
    }
    
    /**
     * Generate a complete response for the given prompt.
     * This is a suspend function that runs on Dispatchers.Default.
     * 
     * @param prompt The input prompt
     * @return Complete generated response
     * @throws LlamaException.GenerationError on generation failure
     * @throws CancellationException if coroutine is cancelled
     */
    suspend fun generate(prompt: String): String
    
    /**
     * Generate a streaming response for the given prompt.
     * Returns a Flow that emits tokens as they are generated.
     * 
     * @param prompt The input prompt
     * @return Flow of generated tokens
     */
    fun generateStream(prompt: String): Flow<String>
    
    /**
     * Check if model is loaded and ready.
     */
    val isLoaded: Boolean
    
    /**
     * Get current configuration.
     */
    val config: LlamaConfig
    
    /**
     * Release native resources.
     * Called automatically if using `use {}` block.
     */
    override fun close()
}
```

### LlamaConfig

```kotlin
data class LlamaConfig(
    // Context
    var contextSize: Int = 2048,
    var batchSize: Int = 512,
    
    // Threading
    var threads: Int = 4,
    
    // Sampling
    var temperature: Float = 0.7f,
    var topP: Float = 0.9f,
    var topK: Int = 40,
    var repeatPenalty: Float = 1.1f,
    
    // Limits
    var maxTokens: Int = 512,
    var stopSequences: List<String> = emptyList(),
    
    // Memory
    var useMmap: Boolean = true,
    var useMlock: Boolean = false,
    
    // GPU
    var gpuLayers: Int = 0
)
```

### LlamaException

```kotlin
sealed class LlamaException(message: String, cause: Throwable? = null) 
    : Exception(message, cause) {
    
    class ModelNotFound(path: String) 
        : LlamaException("Model file not found: $path")
    
    class ModelLoadError(message: String, cause: Throwable? = null) 
        : LlamaException("Failed to load model: $message", cause)
    
    class GenerationError(message: String, cause: Throwable? = null) 
        : LlamaException("Generation failed: $message", cause)
    
    class InvalidConfig(message: String) 
        : LlamaException("Invalid configuration: $message")
    
    class NativeError(code: Int, message: String) 
        : LlamaException("Native error ($code): $message")
}
```

---

## 📦 Supported Models

| Model | Parameters | Quantized Size | Recommended |
|-------|------------|----------------|-------------|
| TinyLlama 1.1B | 1.1B | ~600 MB (Q4_K_M) | Best for mobile |
| Phi-2 | 2.7B | ~1.5 GB (Q4_K_M) | Good balance |
| Llama 3.2 1B | 1B | ~700 MB (Q4_K_M) | High quality |
| Llama 3.2 3B | 3B | ~1.8 GB (Q4_K_M) | Good for tablets |
| Mistral 7B | 7B | ~4 GB (Q4_K_M) | Premium devices only |
| Gemma 2B | 2B | ~1.2 GB (Q4_K_M) | Good alternative |

### Quantization Formats

| Format | Size | Quality | Speed |
|--------|------|---------|-------|
| Q4_0 | Smallest | Lower | Fastest |
| Q4_K_M | Small | Good | Fast |
| Q5_K_M | Medium | Better | Medium |
| Q8_0 | Large | Best | Slower |

---

## 📋 Requirements

- **Android API**: 24+ (Android 7.0 Nougat)
- **Architecture**: arm64-v8a, armeabi-v7a, x86_64
- **RAM**: 
  - Minimum: 4 GB (for small models)
  - Recommended: 6+ GB (for medium models)
- **Storage**: 600 MB - 4 GB (depending on model)

---

## 🛠️ Building from Source

### Prerequisites

- Android Studio Ladybug or newer
- NDK 27.3.13750724 or compatible
- CMake 3.22.1+
- Git (for submodules)

### Steps

```bash
# Clone repository
git clone --recursive https://github.com/it5prasoon/llama-kotlin-android.git
cd llama-kotlin-android

# If you forgot --recursive
git submodule update --init --recursive

# Build
./gradlew assembleRelease
```

---

## 🤝 Contributing

Contributions are welcome! Please read our [Contributing Guidelines](CONTRIBUTING.md) first.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- [llama.cpp](https://github.com/ggerganov/llama.cpp) - The incredible C++ inference engine
- [Kotlin Coroutines](https://github.com/Kotlin/kotlinx.coroutines) - Async programming made easy
- [Android NDK](https://developer.android.com/ndk) - Native development kit

---

<p align="center">
  Made with ❤️ for the Android community
</p>
