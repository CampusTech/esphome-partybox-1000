# Internal types remain isolated in C; WHOLE_ARCHIVE retains late --wrap references.
idf_component_register(SRCS "tws_hook.c" "radio_probe.c" REQUIRES bt WHOLE_ARCHIVE)
idf_component_get_property(bt_library bt COMPONENT_LIB)
idf_component_get_property(bt_directory bt COMPONENT_DIR)
get_target_property(bt_headers ${bt_library} INCLUDE_DIRECTORIES)
target_include_directories(${COMPONENT_LIB} PRIVATE ${bt_headers}
    "${bt_directory}/host/bluedroid/bta/av/include")
