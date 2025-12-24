# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Настройка путей к локальным компонентам
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)
# esp-idf-lib: подключаем только реально используемые компоненты,
# чтобы не собирать весь набор драйверов (и не ловить несовместимости).
set(_ESP_IDF_LIB_COMPONENTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-idf-lib/components")
foreach(_c IN ITEMS esp_idf_lib_helpers i2cdev button pcf8575)
    if(EXISTS "${_ESP_IDF_LIB_COMPONENTS_DIR}/${_c}/CMakeLists.txt")
        list(APPEND EXTRA_COMPONENT_DIRS "${_ESP_IDF_LIB_COMPONENTS_DIR}/${_c}")
    endif()
endforeach()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-idf5-pump5-v3)

# Create a SPIFFS image for the 'storage' partition from the contents of main/storage
spiffs_create_partition_image(storage main/storage FLASH_IN_PROJECT)
