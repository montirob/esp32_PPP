set(srcs "src/esp_modem.c"
        "src/esp_modem_dce_service"
        "src/ec21.c"
        "src/esp_modem_compat.c"
        "src/esp_modem_netif.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES driver lwip DrvNvs)