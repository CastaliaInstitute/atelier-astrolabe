idf_component_register(
    SRCS
      "astrolabe_time.c"
    INCLUDE_DIRS
      "include"
    REQUIRES
      esp_netif
      nvs_flash)
