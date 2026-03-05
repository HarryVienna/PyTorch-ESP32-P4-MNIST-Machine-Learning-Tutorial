FILE(GLOB_RECURSE main_c ${CMAKE_SOURCE_DIR}/main/*.c)
FILE(GLOB_RECURSE main_cpp ${CMAKE_SOURCE_DIR}/main/*.cpp)

idf_component_register(SRCS ${main_c} ${main_cpp}
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_timer esp_driver_ppa esp_mm spiffs)

# Suppress false-positive stringop-overflow from esp-dl headers with -O2
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-stringop-overflow)
