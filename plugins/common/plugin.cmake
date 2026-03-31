# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Common")
set(PLUGIN_DESCRIPTION "Common utilities - DO NOT USE AS STATIC PLUGIN!")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
unset(PLUGIN_REGISTER_ENDPOINT) # This plugin does not have a registration endpoint since it is not meant to be used as a static plugin.
set(PLUGIN_HEADER "${CMAKE_CURRENT_LIST_DIR}/include/${PLUGIN_NAME}/${PLUGIN_NAME}.h")



#
#   NOTE: this isn't a standalone plugin.
#       It is not meant to be built/linked as a Flutter plugin
#       using `STATIC_PLUGIN_DIRS` or as a shared plugin. Instead, it is meant to be used as a common library that other plugins can link against to share code and utilities.
#
