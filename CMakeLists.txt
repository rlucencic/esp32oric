# Oric Atmos emulator for the Makerfabs MaTouch ESP32-S3 Parallel TFT with
# Touch 7" (target esp32s3, the default) or the M5Stack Tab5 (target esp32p4:
# idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig ...)
cmake_minimum_required(VERSION 3.16)

# The shared board runtime and the 6502 core: ../esp32emu (with its copies of
# ESP-IDF's esp_lcd and fatfs for ESP-IDF 5 or 6)
include("${CMAKE_CURRENT_LIST_DIR}/../esp32emu/components.cmake")

# The Tab5: the board's settings from ../esp32emu after this project's
if(IDF_TARGET STREQUAL "esp32p4" OR "$ENV{IDF_TARGET}" STREQUAL "esp32p4")
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
                           "${CMAKE_CURRENT_LIST_DIR}/../esp32emu/boards/m5stack_tab5/sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
if(IDF_TARGET STREQUAL "esp32p4")
    # its own components (the BSP, esp_hosted, ...): its own lock file
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.esp32p4")
endif()
project(esp32oric)

# ROM images (not in git) go into 64 KB slots of the oric_roms partition on
# "idf.py flash" (main/rom_store.c): basic11b.rom (the Atmos's BASIC 1.1) in
# slot 0, basic10.rom (the Oric-1's 1.0) in slot 1, microdis.rom (the
# Microdisc's EPROM) in slot 2
partition_table_get_partition_info(rom_partition "--partition-name oric_roms" "offset")
if(rom_partition)
    foreach(entry "basic11b.rom:0" "basic10.rom:1" "microdis.rom:2")
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 rom)
        list(GET parts 1 slot)
        set(rom_image "${CMAKE_SOURCE_DIR}/roms/${rom}")
        string(REPLACE "." "_" target "${rom}")
        if(EXISTS "${rom_image}")
            math(EXPR offset "${rom_partition} + ${slot} * 65536" OUTPUT_FORMAT HEXADECIMAL)
            esptool_py_flash_target_image(flash "rom_${target}" "${offset}" "${rom_image}" ALWAYS_PLAINTEXT)
        else()
            message(WARNING "${rom_image} missing: it will not be flashed")
        endif()
    endforeach()
endif()
