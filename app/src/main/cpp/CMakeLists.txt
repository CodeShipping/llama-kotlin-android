cmake_minimum_required(VERSION 3.22.1)

project("llama-android" LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for optimization
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")

# ARM NEON optimization for ARM architectures
if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+fp+simd")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+fp+simd")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon -mfloat-abi=softfp")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon -mfloat-abi=softfp")
endif()

# llama.cpp source directory (will be added as submodule)
set(LLAMA_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp)

# Check if llama.cpp exists
if(EXISTS ${LLAMA_CPP_DIR}/CMakeLists.txt)
    message(STATUS "Found llama.cpp at ${LLAMA_CPP_DIR}")
    
    # llama.cpp build options
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LLAMA_STATIC ON CACHE BOOL "" FORCE)
    set(GGML_STATIC ON CACHE BOOL "" FORCE)
    
    # Add llama.cpp as subdirectory
    add_subdirectory(${LLAMA_CPP_DIR} llama.cpp)
    
    # Include directories from llama.cpp
    set(LLAMA_INCLUDE_DIRS
        ${LLAMA_CPP_DIR}/include
        ${LLAMA_CPP_DIR}/ggml/include
        ${LLAMA_CPP_DIR}/common
    )
else()
    message(WARNING "llama.cpp not found at ${LLAMA_CPP_DIR}. Using stub implementation.")
    set(LLAMA_INCLUDE_DIRS "")
endif()

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

# Find required libraries
find_library(log-lib log)
find_library(android-lib android)

# Link libraries
if(EXISTS ${LLAMA_CPP_DIR}/CMakeLists.txt)
    target_link_libraries(llama-android
        llama
        ggml
        ${log-lib}
        ${android-lib}
    )
    target_compile_definitions(llama-android PRIVATE LLAMA_AVAILABLE=1)
else()
    target_link_libraries(llama-android
        ${log-lib}
        ${android-lib}
    )
    target_compile_definitions(llama-android PRIVATE LLAMA_AVAILABLE=0)
endif()

# Export symbols
set_target_properties(llama-android PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
