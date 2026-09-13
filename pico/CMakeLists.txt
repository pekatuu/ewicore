cmake_minimum_required(VERSION 3.20)
project(ewi_synth_pico C)
set(CMAKE_C_STANDARD 11)
# Pico SDKのパスは環境に合わせて -DPICO_SDK_PATH=... で指定
include(pico_sdk_import.cmake)
pico_sdk_init()
add_executable(ewi_synth_pico main.c ../core/ewi_synth.c)
target_include_directories(ewi_synth_pico PRIVATE ../core)
target_link_libraries(ewi_synth_pico pico_stdlib hardware_uart hardware_dma hardware_pio)
pico_enable_stdio_usb(ewi_synth_pico 1)
pico_add_extra_outputs(ewi_synth_pico)
# I2S出力は pico_audio_i2s / 自作PIOドライバをここに追加する
