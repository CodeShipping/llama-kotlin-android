cmake_minimum_required(VERSION 3.22.1)

project("llama-android" LANGUAGES C CXX)

# Version passed from Gradle build
if(NOT DEFINED LIBRARY_VERSION)
    set(LIBRARY_VERSION "0.1.1")
endif()
message(STATUS "Building llama-kotlin-android version: ${LIBRARY_VERSION}")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for optimization
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")

# 16 KB page size compatibility for Android 15+ (required from Nov 2025)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

# ARM NEON optimization for ARM architectures
if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+fp+simd")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+fp+simd")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon -mfloat-abi=softfp")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon -mfloat-abi=softfp")
endif()

# ============================================================================
# llama.cpp Submodule
# ============================================================================
set(LLAMA_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp)

# Check if llama.cpp submodule exists
if(EXISTS ${LLAMA_CPP_DIR}/CMakeLists.txt)
    message(STATUS "Found llama.cpp at ${LLAMA_CPP_DIR}")
    
    # llama.cpp build options
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LLAMA_STATIC ON CACHE BOOL "" FORCE)
    set(GGML_STATIC ON CACHE BOOL "" FORCE)
    set(LLAMA_NATIVE OFF CACHE BOOL "" FORCE)    # Don't use native CPU features (cross-compile)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    
    # Add llama.cpp as subdirectory
    add_subdirectory(${LLAMA_CPP_DIR} llama.cpp)
    
    # Include directories from llama.cpp
    set(LLAMA_INCLUDE_DIRS
        ${LLAMA_CPP_DIR}/include
        ${LLAMA_CPP_DIR}/ggml/include
        ${LLAMA_CPP_DIR}/common
    )
    
    set(LLAMA_AVAILABLE TRUE)
else()
    message(WARNING "llama.cpp not found at ${LLAMA_CPP_DIR}")
    message(WARNING "Run: git submodule update --init --recursive")
    message(WARNING "Using stub implementation...")
    set(LLAMA_AVAILABLE FALSE)
    set(LLAMA_INCLUDE_DIRS "")
endif()

# ============================================================================
# JNI Library
# ============================================================================

# JNI source files
set(JNI_SOURCES
    llama_jni.cpp
    llama_context_wrapper.cpp
)

# Create the shared library
add_library(llama-android SHARED ${JNI_SOURCES})

# Include directories
target_include_directories(llama-android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLAMA_INCLUDE_DIRS}
)

# Find required Android libraries
find_library(log-lib log)
find_library(android-lib android)

# Link libraries
if(LLAMA_AVAILABLE)
    target_link_libraries(llama-android
        llama
        ggml
        ${log-lib}
        ${android-lib}
    )
    target_compile_definitions(llama-android PRIVATE LLAMA_AVAILABLE=1)
    message(STATUS "llama-android will link against llama.cpp")
else()
    target_link_libraries(llama-android
        ${log-lib}
        ${android-lib}
    )
    target_compile_definitions(llama-android PRIVATE LLAMA_AVAILABLE=0)
    message(STATUS "llama-android will use stub implementation")
endif()

# Pass version to C++ code
target_compile_definitions(llama-android PRIVATE
    LIBRARY_VERSION="${LIBRARY_VERSION}"
)

# Export symbols
set_target_properties(llama-android PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
