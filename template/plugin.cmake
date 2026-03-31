# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Template Stuff")
set(PLUGIN_DESCRIPTION "The Template Stuff plugin")

# "Template Stuff" -> "Template_Stuff"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Template_Stuff" -> "template_stuff"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Template Stuff" -> "TemplateStuff"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
set(PLUGIN_REGISTER_ENDPOINT "${camelcase_fname}PluginCApiRegisterWithRegistrar")
set(PLUGIN_HEADER "${CMAKE_CURRENT_LIST_DIR}/include/${PLUGIN_NAME}/${PLUGIN_NAME}_plugin_c_api.h")


#
#  Step defines
#

# PLUGIN_STEP_DEPENDENCIES
#   Declare any dependencies that must be resolved before the plugin library is built.
#   For example, if your plugin depends on a third-party library, you would use
#   find_package() or pkg_check_modules() here to locate it and make it available for
#   linking in the PLUGIN_STEP_LIBRARIES step.
#
macro(PLUGIN_STEP_DEPENDENCIES)

endmacro()

# PLUGIN_STEP_TARGETS
#   List of sourced to include in the plugin's library
#   via add_library(). Examples:
#    - ${PLUGIN_NAME}_plugin_c_api.cc
#    - ${PLUGIN_NAME}_plugin.cc
#    - messages.g.cc
#
set(PLUGIN_DATA_SOURCES
    template_stuff_plugin_c_api.cc
    template_stuff_plugin.cc
    messages.g.cc
)

# PLUGIN_STEP_TARGETS
#   This macro is called after all dependencies and libraries
#   have been declared and is responsible for declaring the plugin
#
macro(PLUGIN_STEP_TARGETS)
    # Declare the plugin library target itself, using the PLUGIN_NAME and PLUGIN_HEADER
    # variables defined above. This should be a call to add_library() that creates either
    # a STATIC or SHARED library based on the BUILD_SHARED_PLUGIN option.
    # For example:
    # add_library(${PLUGIN_NAME} ${_LIB_TYPE}
    #     ${PLUGIN_NAME}_plugin_c_api.cc
    #     ${PLUGIN_NAME}_plugin.cc
    #     messages.g.cc
    # )
    #
    # target_include_directories(${PLUGIN_NAME} PUBLIC
    #     include
    # )

endmacro()

# PLUGIN_STEP_LIBRARIES
#   List of libraries to link against when building the plugin.
#   It's not needed to include the following:
#    - flutter
#    - platform_homescreen
#    - ${PLUGIN_NAME}
#   These are automatically included in the final link step based on the BUILD_SHARED_PLUGIN option.
#
set(PLUGIN_DATA_LIBRARIES
    ""
)