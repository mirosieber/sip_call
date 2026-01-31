idf_component_register(SRCS 
                    INCLUDE_DIRS "include")

# Note: The original component.mk had conditional compilation for g711.o and audio_client.o
# but no src directory exists with .c/.cpp files, so this is a header-only component
