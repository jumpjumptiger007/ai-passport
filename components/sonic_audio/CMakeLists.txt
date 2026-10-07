idf_component_register(
    SRCS "sonic_audio.cpp" "sonic_audio_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp sonic_codec sonic_core sonic_tone_renderer esp_timer freertos heap
)
