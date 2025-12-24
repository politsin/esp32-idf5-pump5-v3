idf_component_register(
  SRCS
    "main.cpp"
    # "task/hx711Task.cpp"
    # "task/uartTask.cpp"
    "task/blinkTask.cpp"
    "task/buttonTask.cpp"
    "task/screenTask.cpp"
    "task/encoderTask.cpp"
    "task/counterTask.cpp"
    "task/telegramTask.cpp"
    "task/timeTask.cpp"
    # "task/stepperTask.cpp"
    # "task/tofTask.cpp"
    # screen
    "screen/display_esp32.c"
    "screen/initSequence.c"
    "screen/power_driver.cpp"
    # "screen/st7789.cpp"
    # util
    "util/config.cpp"
    "util/nvsInit.cpp"
    "util/wifi_manager.cpp"
    "util/telegram_manager.cpp"
    "util/i2c.cpp"
    "util/pcf8575_io.cpp"
    "util/utility.cpp"
    "util/spiffs_fs.cpp"
    "util/web_log.cpp"
    "util/web_server.cpp"
  INCLUDE_DIRS 
    "."
    "util"
    "task"
  REQUIRES
    json
    app_update
    esp_http_client
    esp_http_server
    driver
    spi_flash
    esp_hw_support
    spiffs
    esp_partition
    nvs_flash
    esp_wifi
    esp_https_ota
    esp_lcd
    button
    i2cdev
    pcf8575
    esp32-c3-rotary-encoder
  PRIV_REQUIRES
    mbedtls
)
