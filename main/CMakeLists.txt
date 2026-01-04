file(GLOB_RECURSE MY_SRCS
    "./hal/*.c"
    "./hal/*.cc"
    "./hal/*.cpp"
    "./apps/*.cpp"
)

set(MY_INCLUDE_DIRS 
    "."
    "./assets"
    "./hal"
    "./apps"
)

file(GLOB EXTERN_FILES assets/*)

idf_component_register(SRCS "main.cpp" ${MY_SRCS}
                    INCLUDE_DIRS ${MY_INCLUDE_DIRS}
                    EMBED_FILES ${EXTERN_FILES}
                    REQUIRES usb mooncake_log mooncake M5Unified M5GFX
                             driver sdmmc fatfs nvs_flash esp_wifi esp_adc
                             esp_http_server esp_netif json
)
