# No ESP-IDF dependencies on purpose: tests/module builds the same sources
# as a host library.
idf_component_register(
    SRCS "roomcore.c" "priority_array.c" "points.c"
    INCLUDE_DIRS "include"
)
