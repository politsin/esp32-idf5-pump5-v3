# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Настройка путей к локальным компонентам
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}/components/components
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-idf5-pump5-v2)

# Create a SPIFFS image from the contents of the 'font' directory
spiffs_create_partition_image(storage main/storage FLASH_IN_PROJECT)
