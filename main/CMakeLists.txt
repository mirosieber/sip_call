idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_event esp_netif sip_client audio_client display)

# Set C++14 standard as required by the original component.mk
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c++14)

# Add C++ standard library linker flag (from original Makefile)
target_link_libraries(${COMPONENT_LIB} PRIVATE -lstdc++)
