file(GLOB_RECURSE srcs *.c)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_wifi esp_http_server esp_netif nvs_flash esp_event mdns
)
