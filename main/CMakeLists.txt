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
    "task/stepperTask.cpp"
    "task/tofTask.cpp"
    # screen
    "screen/display_esp32.c"
    "screen/initSequence.c"
    "screen/power_driver.cpp"
    # "screen/st7789.cpp"
    # util
    "util/config.cpp"
    "util/nvsInit.cpp"
    "util/i2c.cpp"
  INCLUDE_DIRS 
    "."
    "util"
    "task"
)
