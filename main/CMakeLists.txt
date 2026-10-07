idf_component_register(
    SRCS "sonic_app.cpp"
         "sonic_app_fingerprint.c"
         "sonic_memory_gate.c"
         "sonic_ui.c"
         "sonic_ui_paging.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_app_format esp_system esp_timer freertos heap
            sonic_audio sonic_core sonic_ggwave_profile sonic_runtime
)
