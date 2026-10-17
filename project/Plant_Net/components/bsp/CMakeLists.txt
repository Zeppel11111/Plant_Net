idf_component_register(
    SRC_DIRS "." "tastek_sensor" "TJC" "soil_sensor"
    INCLUDE_DIRS "include" "tastek_sensor" "TJC" "soil_sensor"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common freertos
    PRIV_REQUIRES driver esp_timer
)
