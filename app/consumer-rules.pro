# Consumer ProGuard rules for llama-kotlin-android library

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the main API classes
-keep class org.codeshipping.llamakotlin.LlamaModel { *; }
-keep class org.codeshipping.llamakotlin.LlamaConfig { *; }
-keep class org.codeshipping.llamakotlin.LlamaNative { *; }

# Keep exception classes
-keep class org.codeshipping.llamakotlin.exception.** { *; }

# Keep callback interfaces
-keep interface org.codeshipping.llamakotlin.** { *; }
