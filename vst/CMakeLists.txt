cmake_minimum_required(VERSION 3.15.0)

project(ewi-vst3
    VERSION 1.0.0
    DESCRIPTION "EWI Axis VA — wind-synth VST3 (shared core with Pico2)"
)

# No GUI examples, no plugin examples from the SDK (we build only our plug-in + validator)
# No system VST3 symlink either (needs admin rights; copy the bundle manually)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/vst3sdk)

smtg_add_vst3plugin(ewi-axis-va
    source/EwiVstIds.h
    source/EwiVstParams.h
    source/EwiVstProcessor.h
    source/EwiVstProcessor.cpp
    source/EwiVstController.h
    source/EwiVstController.cpp
    source/factory.cpp
    ../core/ewi_synth.c
)

# Shared DSP core: build as C++ (MSVC C mode lacks C99 features) with PC quality
set_source_files_properties(../core/ewi_synth.c PROPERTIES LANGUAGE CXX)
target_compile_definitions(ewi-axis-va PRIVATE EWI_QUALITY=1)
# Sources are UTF-8 (Japanese comments) — tell MSVC explicitly
target_compile_options(ewi-axis-va PRIVATE /utf-8)
# Link Steinberg base + sdk (AudioEffect/EditController implementations)
target_link_libraries(ewi-axis-va PRIVATE base pluginterfaces sdk)
target_include_directories(ewi-axis-va PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst3sdk
)
