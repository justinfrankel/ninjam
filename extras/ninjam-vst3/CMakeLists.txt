cmake_minimum_required(VERSION 3.22)

# Fix for old CMake policies in dependencies (ogg/vorbis)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(NINJAM_VST3 VERSION 0.0.1)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(JUCE)

if (WIN32)
    FetchContent_Declare(
        asiosdk
        GIT_REPOSITORY https://github.com/audiosdk/asio.git
        GIT_TAG        main
    )
    FetchContent_MakeAvailable(asiosdk)
endif()

# Fetch Ogg
FetchContent_Declare(
    ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG        v1.3.5
)
FetchContent_MakeAvailable(ogg)

# Help Vorbis find Ogg
set(OGG_INCLUDE_DIR ${ogg_SOURCE_DIR}/include)
set(OGG_LIBRARY ogg)
set(OGG_FOUND TRUE)

# Fetch Vorbis
FetchContent_Declare(
    vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG        v1.3.7
)
FetchContent_MakeAvailable(vorbis)

# Fetch Opus
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.4
)
FetchContent_MakeAvailable(opus)

# Define the plugin
juce_add_plugin(NINJAM_VST3
    COMPANY_NAME "AndyMcProducer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Njv3
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "NINJAM VST3"
)

# Generate JuceHeader.h
juce_generate_juce_header(NINJAM_VST3)

# WDL and NINJAM sources
get_filename_component(NINJAMPLUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if (EXISTS "${NINJAMPLUS_ROOT}/WDL" AND EXISTS "${NINJAMPLUS_ROOT}/ninjam/njclient.cpp")
    set(NINJAM_ROOT "${NINJAMPLUS_ROOT}")
    set(WDL_ROOT "${NINJAMPLUS_ROOT}/WDL")
    set(NINJAM_SRC_ROOT "${NINJAMPLUS_ROOT}/ninjam")
else()
    set(NINJAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ninjam")
    set(WDL_ROOT "${NINJAM_ROOT}/WDL")
    set(NINJAM_SRC_ROOT "${NINJAM_ROOT}/ninjam")
endif()

# Define sources
target_sources(NINJAM_VST3 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/StandaloneApp.cpp
    
    # NINJAM Core
    ${NINJAM_SRC_ROOT}/njclient.cpp
    ${NINJAM_SRC_ROOT}/netmsg.cpp
    ${NINJAM_SRC_ROOT}/mpb.cpp
    ${NINJAM_SRC_ROOT}/njmisc.cpp
    
    # WDL sources needed by NINJAM
    ${WDL_ROOT}/jnetlib/asyncdns.cpp
    ${WDL_ROOT}/jnetlib/connection.cpp
    ${WDL_ROOT}/jnetlib/httpget.cpp
    ${WDL_ROOT}/jnetlib/util.cpp
    ${WDL_ROOT}/jnetlib/listen.cpp
    ${WDL_ROOT}/jnetlib/webserver.cpp
    
    ${WDL_ROOT}/sha.cpp
    ${WDL_ROOT}/rng.cpp
    ${WDL_ROOT}/resample.cpp # Might be needed
    ${WDL_ROOT}/fft.c
)

# Include directories
target_include_directories(NINJAM_VST3 PRIVATE
    Source
    ${NINJAM_ROOT}
    ${NINJAM_SRC_ROOT}
    ${WDL_ROOT}
    ${ogg_SOURCE_DIR}/include
    ${vorbis_SOURCE_DIR}/include
    ${vorbis_SOURCE_DIR}/lib # for vorbis internal headers if needed
    ${opus_SOURCE_DIR}/include
    $<$<BOOL:${WIN32}>:${asiosdk_SOURCE_DIR}/common>
    $<$<BOOL:${WIN32}>:${asiosdk_SOURCE_DIR}/host>
)

# Link libraries
target_link_libraries(NINJAM_VST3 PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_osc
    ogg
    vorbis
    vorbisenc
    vorbisfile
    opus
)

# Compiler definitions
target_compile_definitions(NINJAM_VST3 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
    WDL_NO_DEFINE_MINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<BOOL:${WIN32}>:JUCE_ASIO=1>
)

if (WIN32)
    target_sources(NINJAM_VST3 PRIVATE ${WDL_ROOT}/win32_utf8.c)
    target_link_libraries(NINJAM_VST3 PRIVATE ws2_32 winmm)
endif()

if (TARGET NINJAM_VST3_Standalone)
    add_custom_target(NINJAM_VST3_SyncWebAssets
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NINJAM_VST3_Standalone>/advanced-vdo-client"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/advanced-vdo-client/server.js"
            "$<TARGET_FILE_DIR:NINJAM_VST3_Standalone>/advanced-vdo-client/server.js"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/advanced-vdo-client/index.html"
            "$<TARGET_FILE_DIR:NINJAM_VST3_Standalone>/advanced-vdo-client/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/advanced-vdo-client/icon.png"
            "$<TARGET_FILE_DIR:NINJAM_VST3_Standalone>/advanced-vdo-client/icon.png"
        VERBATIM
    )
    add_dependencies(NINJAM_VST3_Standalone NINJAM_VST3_SyncWebAssets)
endif()
