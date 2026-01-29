cmake_minimum_required(VERSION 3.22)
project(HachiTune VERSION 1.0.0)

include(FetchContent)
include(cmake/Asio.cmake)

# ONNX Runtime configuration
option(USE_GPU "Enable GPU acceleration for ONNX Runtime (not supported on macOS)" OFF)
option(USE_CUDA "Enable CUDA execution provider" OFF)
option(USE_DIRECTML "Enable DirectML execution provider (Windows only)" OFF)
option(USE_BUNDLED_CUDA_RUNTIME "Bundle minimal CUDA runtime DLLs (Windows only)" OFF)
option(USE_BUNDLED_DIRECTML_RUNTIME "Bundle DirectML runtime DLL (Windows only)" OFF)
option(USE_ASIO "Enable ASIO support (Windows only)" OFF)
set(CUDA_REDIST_URL "" CACHE STRING "Optional URL to download CUDA runtime redistributable zip")
set(DIRECTML_REDIST_URL "" CACHE STRING "Optional URL to download DirectML redistributable zip")
set(ONNXRUNTIME_VERSION "1.17.3" CACHE STRING "ONNX Runtime version")

# Check that CUDA and DirectML are not both enabled
if(USE_CUDA AND USE_DIRECTML)
    message(FATAL_ERROR "ERROR: USE_CUDA and USE_DIRECTML cannot be enabled at the same time. "
                        "DirectML requires a separate version of ONNX Runtime. "
                        "Please enable only one of them.")
endif()

# If CUDA is requested, force GPU pipeline on
if(USE_CUDA)
    set(USE_GPU ON)
    message(STATUS "USE_CUDA=ON -> enabling USE_GPU automatically")
endif()

# If DirectML is requested, force GPU pipeline on
if(USE_DIRECTML)
    set(USE_GPU ON)
    message(STATUS "USE_DIRECTML=ON -> enabling USE_GPU automatically")
endif()

set(HACHITUNE_ASIO_ENABLED OFF)
if(USE_ASIO AND WIN32)
    hachitune_setup_asio(ASIO_INCLUDE_DIR)
    set(HACHITUNE_ASIO_ENABLED ON)
    message(STATUS "ASIO support: ENABLED")
elseif(USE_ASIO)
    message(WARNING "USE_ASIO is enabled but ASIO is only supported on Windows")
endif()

set(HACHITUNE_JUCE_ASIO 0)
set(HACHITUNE_JUCE_ASIO_EXTERNAL 0)
if(HACHITUNE_ASIO_ENABLED)
    set(HACHITUNE_JUCE_ASIO 1)
    set(HACHITUNE_JUCE_ASIO_EXTERNAL 1)
endif()

# Check if ONNX Runtime already exists in build directory
set(ONNXRUNTIME_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps/onnxruntime-src")
if(EXISTS "${ONNXRUNTIME_CACHE_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "ONNX Runtime already downloaded, skipping download")
    set(ONNXRUNTIME_ROOT ${ONNXRUNTIME_CACHE_DIR})
else()
    # Detect platform and set download URL
    if(WIN32)
        if(USE_CUDA)
            # CUDA requires CUDA-specific ONNX Runtime build
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-gpu-cuda12-${ONNXRUNTIME_VERSION}.zip")
            message(STATUS "Downloading ONNX Runtime GPU CUDA12 package (Windows)")
        elseif(USE_DIRECTML)
            # DirectML uses the NuGet package (NuGet packages are ZIP files)
            set(ONNXRUNTIME_URL "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${ONNXRUNTIME_VERSION}")
            message(STATUS "Downloading ONNX Runtime DirectML package from NuGet (Windows)")
        else()
            # CPU-only package
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
            message(STATUS "Downloading ONNX Runtime CPU package (Windows)")
        endif()
    elseif(APPLE)
        if(USE_GPU)
            message(WARNING "GPU acceleration is not supported on macOS, falling back to CPU")
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
        else()
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x86_64-${ONNXRUNTIME_VERSION}.tgz")
        endif()
    else()
        if(USE_GPU)
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-gpu-${ONNXRUNTIME_VERSION}.tgz")
        else()
            set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
        endif()
    endif()

    message(STATUS "Downloading ONNX Runtime from: ${ONNXRUNTIME_URL}")

    FetchContent_Declare(
        onnxruntime
        URL ${ONNXRUNTIME_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})
endif()

# Handle different directory structures for DirectML NuGet package vs regular packages
if(WIN32 AND USE_DIRECTML)
    # DirectML NuGet package has different structure: runtimes/win-x64/native/
    set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_ROOT}/build/native/include)
    set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime.lib)
    set(ONNXRUNTIME_DLL_DIR ${ONNXRUNTIME_ROOT}/runtimes/win-x64/native)
else()
    set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_ROOT}/include)
    if(WIN32)
        set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib)
        set(ONNXRUNTIME_DLL_DIR ${ONNXRUNTIME_ROOT}/lib)
    else()
        find_library(ONNXRUNTIME_LIBRARY onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib NO_DEFAULT_PATH)
    endif()
endif()

if(EXISTS ${ONNXRUNTIME_INCLUDE_DIR} AND ONNXRUNTIME_LIBRARY)
    set(ONNXRUNTIME_FOUND TRUE)
    message(STATUS "ONNX Runtime configured:")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
    if(USE_GPU AND NOT APPLE)
        message(STATUS "  GPU acceleration: ENABLED")
    else()
        message(STATUS "  GPU acceleration: DISABLED")
    endif()
else()
    set(ONNXRUNTIME_FOUND FALSE)
    message(WARNING "ONNX Runtime configuration failed")
endif()

# Add JUCE as a subdirectory (git submodule in third_party/)
add_subdirectory(third_party/JUCE)

# Add Kiwi constraint solver library
add_subdirectory(third_party/kiwi)

# Create GUI application
juce_add_gui_app(HachiTune
    PRODUCT_NAME "HachiTune"
    COMPANY_NAME "OpenVPI"
    BUNDLE_ID "com.openvpi.hachitune"
    COMPANY_COPYRIGHT "Copyright 2026"
    MICROPHONE_PERMISSION_ENABLED TRUE
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_EXTENSIONS "wav;mp3;flac;ogg")

# AAX SDK configuration (user must set AAX_SDK_PATH environment variable)
if(DEFINED ENV{AAX_SDK_PATH} AND EXISTS "$ENV{AAX_SDK_PATH}")
    juce_set_aax_sdk_path($ENV{AAX_SDK_PATH})
    set(AAX_AVAILABLE TRUE)
    message(STATUS "AAX SDK found at: $ENV{AAX_SDK_PATH}")
else()
    set(AAX_AVAILABLE FALSE)
    message(STATUS "AAX SDK not found - AAX format will not be built")
endif()

# ARA SDK configuration (bundled as submodule)
set(ARA_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ARA_SDK")
if(EXISTS "${ARA_SDK_PATH}/ARA_API")
    juce_set_ara_sdk_path(${ARA_SDK_PATH})
    set(ARA_AVAILABLE TRUE)
    message(STATUS "ARA SDK found at: ${ARA_SDK_PATH}")
else()
    set(ARA_AVAILABLE FALSE)
    message(STATUS "ARA SDK not found - run: git submodule update --init --recursive")
endif()

# Create VST3 plugin
juce_add_plugin(HachiTunePlugin
    PRODUCT_NAME "HachiTune"
    COMPANY_NAME "OpenVPI"
    BUNDLE_ID "com.openvpi.hachitune"
    PLUGIN_MANUFACTURER_CODE OPVP
    PLUGIN_CODE PTE1
    FORMATS VST3 AU $<$<BOOL:${AAX_AVAILABLE}>:AAX>
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_ARA_EFFECT $<BOOL:${ARA_AVAILABLE}>
    MICROPHONE_PERMISSION_ENABLED TRUE
    FILE_SHARING_ENABLED TRUE)

# ARA compile definitions
if(ARA_AVAILABLE)
    target_compile_definitions(HachiTunePlugin PRIVATE
        JucePlugin_Enable_ARA=1)
endif()

# Add source files using GLOB_RECURSE
file(GLOB_RECURSE PITCH_EDITOR_COMMON_SOURCES
    "Source/Audio/*.cpp" "Source/Audio/*.h"
    "Source/UI/*.cpp" "Source/UI/*.h"
    "Source/Models/*.cpp" "Source/Models/*.h"
    "Source/Utils/*.cpp" "Source/Utils/*.h" "Source/Utils/*.mm"
    "Source/JuceHeader.h"
    "Source/Plugin/HostCompatibility.cpp" "Source/Plugin/HostCompatibility.h")

# Ensure new preview utility is always built even if glob cache is stale.
list(APPEND PITCH_EDITOR_COMMON_SOURCES
    "Source/Utils/BasePitchPreview.cpp"
    "Source/Utils/BasePitchPreview.h")

file(GLOB PLUGIN_SOURCES
    "Source/Plugin/PluginProcessor.cpp" "Source/Plugin/PluginProcessor.h"
    "Source/Plugin/PluginEditor.cpp" "Source/Plugin/PluginEditor.h"
    "Source/Plugin/ARADocumentController.cpp" "Source/Plugin/ARADocumentController.h"
    "Source/Plugin/NonAraCaptureController.cpp" "Source/Plugin/NonAraCaptureController.h")

target_sources(HachiTune PRIVATE
    Source/Main.cpp
    ${PITCH_EDITOR_COMMON_SOURCES})

target_sources(HachiTunePlugin PRIVATE
    ${PLUGIN_SOURCES}
    ${PITCH_EDITOR_COMMON_SOURCES})

# Binary data for SVG icons
file(GLOB SVG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/svg/*.svg")
juce_add_binary_data(BinaryData SOURCES ${SVG_FILES})

# Link JUCE modules
target_link_libraries(HachiTune PRIVATE
    BinaryData
    kiwi::kiwi
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_link_libraries(HachiTunePlugin PRIVATE
    BinaryData
    kiwi::kiwi
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_plugin_client
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(HACHITUNE_ASIO_ENABLED)
    target_include_directories(HachiTune PRIVATE ${ASIO_INCLUDE_DIR})
    target_include_directories(HachiTunePlugin PRIVATE ${ASIO_INCLUDE_DIR})
endif()

# Link ONNX Runtime if found
if(ONNXRUNTIME_FOUND)
    target_include_directories(HachiTune PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(HachiTune PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(HachiTune PRIVATE HAVE_ONNXRUNTIME=1)

    target_include_directories(HachiTunePlugin PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(HachiTunePlugin PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(HachiTunePlugin PRIVATE HAVE_ONNXRUNTIME=1)

    # Pass GPU option to code (macOS always uses CPU)
    if(USE_GPU AND NOT APPLE)
        target_compile_definitions(HachiTune PRIVATE ONNXRUNTIME_USE_GPU=1)
        target_compile_definitions(HachiTunePlugin PRIVATE ONNXRUNTIME_USE_GPU=1)
    endif()

    # Enable CUDA if requested
    if(USE_CUDA)
        target_compile_definitions(HachiTune PRIVATE USE_CUDA=1)
        target_compile_definitions(HachiTunePlugin PRIVATE USE_CUDA=1)
        message(STATUS "CUDA execution provider: ENABLED")
    endif()

    # Enable DirectML if requested (Windows only)
    if(USE_DIRECTML AND WIN32)
        target_compile_definitions(HachiTune PRIVATE USE_DIRECTML=1)
        target_compile_definitions(HachiTunePlugin PRIVATE USE_DIRECTML=1)
        message(STATUS "DirectML execution provider: ENABLED")
    endif()

    # Copy ONNX Runtime DLLs to output directory on Windows
    if(WIN32)
        get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARY} DIRECTORY)
        # Try lib directory first (most common), then bin directory
        file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/*.dll")
        if(NOT ONNXRUNTIME_DLLS)
            file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/../bin/*.dll")
        endif()
        if(ONNXRUNTIME_DLLS)
            foreach(DLL ${ONNXRUNTIME_DLLS})
                message(STATUS "Will copy ONNX Runtime DLL: ${DLL}")
                add_custom_command(TARGET HachiTune POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:HachiTune>")

                add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>")
            endforeach()
        else()
            message(WARNING "No ONNX Runtime DLLs found to copy")
        endif()
        
        # Copy CUDA runtime DLLs if CUDA is enabled
        if(USE_CUDA)
            # Optional bundled CUDA runtime (redistributable) to avoid system dependency
            # If USE_BUNDLED_CUDA_RUNTIME is ON, we first look for a local redistributable dir,
            # otherwise try to download from CUDA_REDIST_URL (if provided).
            if(USE_BUNDLED_CUDA_RUNTIME)
                set(CUDA_REDIST_DIR "${CMAKE_SOURCE_DIR}/vendor/cuda/bin")
                if(NOT EXISTS "${CUDA_REDIST_DIR}")
                    set(CUDA_REDIST_DIR "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                endif()

                # Download redistributable if URL is provided and dir not present
                if(NOT EXISTS "${CUDA_REDIST_DIR}" AND CUDA_REDIST_URL)
                    message(STATUS "Downloading CUDA runtime redistributable from ${CUDA_REDIST_URL}")
                    file(DOWNLOAD "${CUDA_REDIST_URL}"
                        "${CMAKE_BINARY_DIR}/cuda_redist.zip"
                        SHOW_PROGRESS)
                    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cuda_redist")
                    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_BINARY_DIR}/cuda_redist.zip"
                                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/cuda_redist")
                    # Try common layout: cuda_redist/bin
                    if(EXISTS "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                        set(CUDA_REDIST_DIR "${CMAKE_BINARY_DIR}/cuda_redist/bin")
                    endif()
                endif()
            endif()

            # Common CUDA installation paths
            set(CUDA_PATHS
                "$ENV{CUDA_PATH}/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.2/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.9/bin"
            )
            
            # CUDA runtime DLLs needed by ONNX Runtime
            set(CUDA_DLLS
                "cublasLt64_12.dll"
                "cublas64_12.dll"
                "cudnn64_8.dll"
                "cufft64_12.dll"
                "curand64_12.dll"
                "cusolver64_12.dll"
                "cusparse64_12.dll"
                "cudart64_12.dll"
            )
            
            # Also try version 11 DLLs
            list(APPEND CUDA_DLLS
                "cublasLt64_11.dll"
                "cublas64_11.dll"
                "cufft64_11.dll"
                "curand64_11.dll"
                "cusolver64_11.dll"
                "cusparse64_11.dll"
                "cudart64_11.dll"
            )
            
            # Find CUDA bin directory (prefer bundled redistributable if present)
            set(CUDA_BIN_DIR "")
            if(USE_BUNDLED_CUDA_RUNTIME AND EXISTS "${CUDA_REDIST_DIR}")
                set(CUDA_BIN_DIR "${CUDA_REDIST_DIR}")
                message(STATUS "Using bundled CUDA runtime from: ${CUDA_BIN_DIR}")
            endif()

            foreach(CUDA_PATH ${CUDA_PATHS})
                if(NOT CUDA_BIN_DIR AND EXISTS "${CUDA_PATH}")
                    set(CUDA_BIN_DIR "${CUDA_PATH}")
                    message(STATUS "Found CUDA bin directory: ${CUDA_BIN_DIR}")
                    break()
                endif()
            endforeach()
            
            if(CUDA_BIN_DIR)
                foreach(CUDA_DLL ${CUDA_DLLS})
                    set(CUDA_DLL_PATH "${CUDA_BIN_DIR}/${CUDA_DLL}")
                    if(EXISTS "${CUDA_DLL_PATH}")
                        message(STATUS "Will copy CUDA DLL: ${CUDA_DLL}")
                        add_custom_command(TARGET HachiTune POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CUDA_DLL_PATH}"
                            "$<TARGET_FILE_DIR:HachiTune>")
                        
                        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CUDA_DLL_PATH}"
                            "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>")
                    endif()
                endforeach()
            else()
                message(WARNING "CUDA bin directory not found. CUDA runtime DLLs will not be copied.")
                message(WARNING "Please ensure CUDA Toolkit is installed and CUDA_PATH environment variable is set.")
                message(WARNING "Or manually copy CUDA DLLs from CUDA installation to the output directory.")
            endif()
        endif()

        # Copy DirectML runtime DLL if explicitly requested (Windows only)
        # Note: DirectML is typically included in Windows 10/11 and will be loaded from the system
        if(USE_DIRECTML AND USE_BUNDLED_DIRECTML_RUNTIME)
            set(DML_DLLS "DirectML.dll")
            set(DML_BIN_DIR "")

            # DirectML version for auto-download
            if(NOT DIRECTML_VERSION)
                set(DIRECTML_VERSION "1.15.4")
            endif()

            # Check for pre-existing DirectML in vendor directory
            set(DML_REDIST_DIR "${CMAKE_SOURCE_DIR}/vendor/directml/bin")

            # If not found, check/download to build directory
            if(NOT EXISTS "${DML_REDIST_DIR}/DirectML.dll")
                set(DML_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/directml-src")
                set(DML_REDIST_DIR "${DML_DOWNLOAD_DIR}/bin/x64-win")

                # Auto-download from NuGet if not present
                if(NOT EXISTS "${DML_REDIST_DIR}/DirectML.dll")
                    set(DML_NUGET_URL "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${DIRECTML_VERSION}")
                    set(DML_NUPKG_PATH "${CMAKE_BINARY_DIR}/_deps/directml.nupkg")

                    message(STATUS "Downloading DirectML ${DIRECTML_VERSION} from NuGet...")
                    file(DOWNLOAD "${DML_NUGET_URL}" "${DML_NUPKG_PATH}"
                        SHOW_PROGRESS
                        STATUS DML_DOWNLOAD_STATUS)

                    list(GET DML_DOWNLOAD_STATUS 0 DML_DOWNLOAD_RESULT)
                    if(DML_DOWNLOAD_RESULT EQUAL 0)
                        message(STATUS "Extracting DirectML package...")
                        file(MAKE_DIRECTORY "${DML_DOWNLOAD_DIR}")
                        execute_process(
                            COMMAND ${CMAKE_COMMAND} -E tar xf "${DML_NUPKG_PATH}"
                            WORKING_DIRECTORY "${DML_DOWNLOAD_DIR}"
                            RESULT_VARIABLE DML_EXTRACT_RESULT)

                        if(NOT DML_EXTRACT_RESULT EQUAL 0)
                            message(WARNING "Failed to extract DirectML package")
                        endif()
                    else()
                        message(WARNING "Failed to download DirectML: ${DML_DOWNLOAD_STATUS}")
                    endif()
                endif()
            endif()

            if(EXISTS "${DML_REDIST_DIR}/DirectML.dll")
                set(DML_BIN_DIR "${DML_REDIST_DIR}")
                message(STATUS "Using bundled DirectML ${DIRECTML_VERSION} from: ${DML_BIN_DIR}")

                foreach(DML_DLL ${DML_DLLS})
                    set(DML_DLL_PATH "${DML_BIN_DIR}/${DML_DLL}")
                    if(EXISTS "${DML_DLL_PATH}")
                        message(STATUS "Will copy DirectML DLL: ${DML_DLL_PATH}")
                        add_custom_command(TARGET HachiTune POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DML_DLL_PATH}"
                            "$<TARGET_FILE_DIR:HachiTune>")

                        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${DML_DLL_PATH}"
                            "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>")
                    endif()
                endforeach()
            else()
                message(WARNING "USE_BUNDLED_DIRECTML_RUNTIME is ON but DirectML.dll not found.")
                message(WARNING "Check network connection or place DirectML.dll in vendor/directml/bin/")
            endif()
        elseif(USE_DIRECTML)
            message(STATUS "DirectML will be loaded from the system (included in Windows 10/11)")
        endif()
    elseif(APPLE)
        # macOS: Embed dylib in app bundle and plugin bundles
        get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARY} DIRECTORY)
        file(GLOB ONNXRUNTIME_DYLIBS "${ONNXRUNTIME_LIB_DIR}/*.dylib")
        if(ONNXRUNTIME_DYLIBS)
            foreach(DYLIB ${ONNXRUNTIME_DYLIBS})
                message(STATUS "Will embed ONNX Runtime dylib: ${DYLIB}")
                # Standalone app
                add_custom_command(TARGET HachiTune POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Frameworks/")
                # VST3 plugin
                add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Frameworks/")
                # AU plugin
                add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Frameworks/")
            endforeach()
            # Fix rpath for all targets using install_name_tool
            add_custom_command(TARGET HachiTune POST_BUILD
                COMMAND install_name_tool -add_rpath "@executable_path/../Frameworks" "$<TARGET_FILE:HachiTune>" 2>/dev/null || true)
            add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
                COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks" "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/MacOS/HachiTune" 2>/dev/null || true)
            add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
                COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks" "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/MacOS/HachiTune" 2>/dev/null || true)
        endif()
    endif()
endif()

# Set C++ standard
target_compile_features(HachiTune PRIVATE cxx_std_17)
target_compile_features(HachiTunePlugin PRIVATE cxx_std_17)

# Platform-specific settings
if(WIN32)
    # NOMINMAX prevents Windows.h from defining min/max macros that conflict with std::min/std::max
    target_compile_definitions(HachiTune PRIVATE
        NOMINMAX
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=${HACHITUNE_JUCE_ASIO}
        JUCE_ASIO_USE_EXTERNAL_SDK=${HACHITUNE_JUCE_ASIO_EXTERNAL})

    target_compile_definitions(HachiTunePlugin PRIVATE
        NOMINMAX
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_ASIO=${HACHITUNE_JUCE_ASIO}
        JUCE_ASIO_USE_EXTERNAL_SDK=${HACHITUNE_JUCE_ASIO_EXTERNAL})
elseif(APPLE)
    target_compile_definitions(HachiTune PRIVATE
        JUCE_COREAUDIO=1)

    target_compile_definitions(HachiTunePlugin PRIVATE
        JUCE_COREAUDIO=1)
else()
    target_compile_definitions(HachiTune PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1)

    target_compile_definitions(HachiTunePlugin PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1)
endif()

# Common definitions
target_compile_definitions(HachiTune PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HachiTune,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HachiTune,JUCE_VERSION>")

target_compile_definitions(HachiTunePlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HachiTunePlugin,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HachiTunePlugin,JUCE_VERSION>")

# Check required models
set(MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models")
set(REQUIRED_MODELS
    pc_nsf_hifigan.onnx
    fcpe.onnx
    some.onnx
    rmvpe.onnx
    cent_table.bin
    mel_filterbank.bin
)

# Check required models
foreach(MODEL ${REQUIRED_MODELS})
    if(NOT EXISTS "${MODELS_DIR}/${MODEL}")
        message(FATAL_ERROR "Required model not found: ${MODELS_DIR}/${MODEL}")
    endif()
endforeach()
message(STATUS "All required models found")

# Copy all files from models directory to targets
file(GLOB ALL_MODEL_FILES "${MODELS_DIR}/*")
foreach(MODEL_PATH ${ALL_MODEL_FILES})
    get_filename_component(MODEL_NAME "${MODEL_PATH}" NAME)
    if(APPLE)
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_PATH}" "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/models/${MODEL_NAME}")
        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_PATH}" "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/models/${MODEL_NAME}")
        add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_PATH}" "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/models/${MODEL_NAME}")
    else()
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HachiTune>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_PATH}" "$<TARGET_FILE_DIR:HachiTune>/models/${MODEL_NAME}")
        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_PATH}" "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>/models/${MODEL_NAME}")
    endif()
endforeach()

# Copy language files
set(LANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/lang")
file(GLOB LANG_FILES "${LANG_DIR}/*.json")
if(LANG_FILES)
    message(STATUS "Found language files: ${LANG_FILES}")
    if(APPLE)
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/lang/")
        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/lang/")
        add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/lang/")
    else()
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HachiTune>/lang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LANG_FILES} "$<TARGET_FILE_DIR:HachiTune>/lang/")
    endif()
endif()

# Copy font files
set(FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts")
file(GLOB FONT_FILES "${FONTS_DIR}/*.otf" "${FONTS_DIR}/*.ttf")
if(FONT_FILES)
    message(STATUS "Found font files: ${FONT_FILES}")
    if(APPLE)
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILES} "$<TARGET_BUNDLE_DIR:HachiTune>/Contents/Resources/fonts/")
        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILES} "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>/Contents/Resources/fonts/")
        add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILES} "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>/Contents/Resources/fonts/")
    else()
        add_custom_command(TARGET HachiTune POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HachiTune>/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILES} "$<TARGET_FILE_DIR:HachiTune>/fonts/")
        add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILES} "$<TARGET_FILE_DIR:HachiTunePlugin_VST3>/fonts/")
    endif()
endif()

# Re-sign bundles on macOS after all resources are copied
if(APPLE)
    add_custom_command(TARGET HachiTune POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:HachiTune>" 2>/dev/null || true
        COMMENT "Re-signing HachiTune bundle")
    add_custom_command(TARGET HachiTunePlugin_VST3 POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:HachiTunePlugin_VST3>" 2>/dev/null || true
        COMMENT "Re-signing VST3 bundle")
    add_custom_command(TARGET HachiTunePlugin_AU POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:HachiTunePlugin_AU>" 2>/dev/null || true
        COMMENT "Re-signing AU bundle")
endif()
