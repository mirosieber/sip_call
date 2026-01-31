idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES driver)

# Note: The original component.mk had conditional compilation for g711.o and audio_client.o
# but no src directory exists with .c/.cpp files, so this is a header-only component
