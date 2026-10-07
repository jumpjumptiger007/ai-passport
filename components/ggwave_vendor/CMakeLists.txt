idf_component_register(
    SRCS "../../ggwave_vendor/src/ggwave.cpp"
    INCLUDE_DIRS "../../ggwave_vendor/include"
    PRIV_INCLUDE_DIRS "../../ggwave_vendor/src"
)

# Keep the pinned donor source byte-for-byte intact. Its legacy %d format
# diagnostic for size_t is emitted by ESP-IDF's warning-as-error defaults.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)
