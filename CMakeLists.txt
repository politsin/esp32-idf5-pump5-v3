# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# set(EXTRA_COMPONENT_DIRS W:/proj/esp-idf-lib-UncleRus/components)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}\\..\\esp-idf-lib-UncleRus\\components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-idf5-blink)

list(APPEND EXTRA_COMPONENT_DIRS hx711)
list(APPEND EXTRA_COMPONENT_DIRS i2cdev)
list(APPEND EXTRA_COMPONENT_DIRS button)
list(APPEND EXTRA_COMPONENT_DIRS st7789)
list(APPEND EXTRA_COMPONENT_DIRS DendoStepper)
list(APPEND EXTRA_COMPONENT_DIRS esp32-c3-rotary-encoder)

## Tests
# set(EXTRA_COMPONENT_DIRS_OTHER ${CMAKE_CURRENT_SOURCE_DIR}\\..\\esp-idf-lib-other\\components)
# list(APPEND EXTRA_COMPONENT_DIRS_OTHER st7789)
# list(APPEND EXTRA_COMPONENT_DIRS_OTHER esp32-c3-rotary-encoder)

# Create a SPIFFS image from the contents of the 'font' directory
spiffs_create_partition_image(storage data FLASH_IN_PROJECT)
