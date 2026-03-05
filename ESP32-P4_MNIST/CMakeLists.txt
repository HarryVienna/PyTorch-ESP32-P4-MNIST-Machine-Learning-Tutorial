# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# GCC 14.2 false-positive stringop-overflow in esp-dl with -O2
idf_build_set_property(COMPILE_OPTIONS "-Wno-stringop-overflow" APPEND)

project(LVGL-HelloWorld)

spiffs_create_partition_image(model spiffs FLASH_IN_PROJECT)
