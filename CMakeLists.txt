cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_mcp)

# Fonts and other runtime assets live in the storage partition.
spiffs_create_partition_image(storage data FLASH_IN_PROJECT)
