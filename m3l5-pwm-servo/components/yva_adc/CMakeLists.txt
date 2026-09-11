idf_component_register(
    SRCS "yva_adc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_adc esp_driver_gpio
)
