idf_component_register(
    SRC_DIRS "." "../APP"
    INCLUDE_DIRS "." "../APP"
    REQUIRES bsp Wifi esp_common freertos log mqtt mqtt_service
)
