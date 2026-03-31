# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Firebase Core")
set(PLUGIN_DESCRIPTION "The Firebase Core plugin")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
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
#
macro(PLUGIN_STEP_DEPENDENCIES)
    if (NOT DEFINED FIREBASE_CPP_SDK_DIR)
        message(FATAL_ERROR "FIREBASE_CPP_SDK_DIR is not set")
    endif ()
    if (NOT DEFINED FIREBASE_SDK_LIBDIR)
        message(FATAL_ERROR "FIREBASE_SDK_LIBDIR is not set")
    endif ()

    pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)
    pkg_check_modules(SECRET REQUIRED IMPORTED_TARGET libsecret-1)

    set(CMAKE_THREAD_PREFER_PTHREAD ON)
    include(FindThreads)

    add_library(firebase_sdk INTERFACE)

    target_include_directories(firebase_sdk INTERFACE
            ${FIREBASE_CPP_SDK_DIR}
            ${FIREBASE_CPP_SDK_DIR}/app/src/include
            ${FIREBASE_CPP_SDK_DIR}/auth/src/include
            ${FIREBASE_CPP_SDK_DIR}/firestore/src/include
            ${FIREBASE_CPP_SDK_DIR}/database/src/include
            ${FIREBASE_CPP_SDK_DIR}/storage/src/include
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore/Firestore/core/include
    )

    target_link_libraries(firebase_sdk INTERFACE
            ${FIREBASE_SDK_LIBDIR}/app/libfirebase_app.a
            ${FIREBASE_SDK_LIBDIR}/app/rest/libfirebase_rest_lib.a
            ${FIREBASE_SDK_LIBDIR}/liblibuWS.a
            ${FIREBASE_SDK_LIBDIR}/dynamic_links/libfirebase_dynamic_links.a
            ${FIREBASE_SDK_LIBDIR}/database/libfirebase_database.a
            ${FIREBASE_SDK_LIBDIR}/app_check/libfirebase_app_check.a
            ${FIREBASE_SDK_LIBDIR}/auth/libfirebase_auth.a
            ${FIREBASE_SDK_LIBDIR}/functions/libfirebase_functions.a
            ${FIREBASE_SDK_LIBDIR}/messaging/libfirebase_messaging.a
            ${FIREBASE_SDK_LIBDIR}/gma/libfirebase_gma.a
            ${FIREBASE_SDK_LIBDIR}/analytics/libfirebase_analytics.a
            ${FIREBASE_SDK_LIBDIR}/installations/libfirebase_installations.a
            ${FIREBASE_SDK_LIBDIR}/storage/libfirebase_storage.a
            ${FIREBASE_SDK_LIBDIR}/firestore/libfirebase_firestore.a
            ${FIREBASE_SDK_LIBDIR}/remote_config/libfirebase_remote_config.a
            ${FIREBASE_SDK_LIBDIR}/external/src/boringssl-build/ssl/libssl.a
            ${FIREBASE_SDK_LIBDIR}/external/src/boringssl-build/crypto/libcrypto.a
            ${FIREBASE_SDK_LIBDIR}/external/src/libuv-build/libuv_a.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/Firestore/core/libfirestore_core.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/Firestore/core/libfirestore_nanopb.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/Firestore/core/libfirestore_util.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/Firestore/Protos/libfirestore_protos_nanopb.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/leveldb-build/libleveldb.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/nanopb-build/libprotobuf-nanopb.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libgrpc++.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libupb.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libgrpc.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/re2/libre2.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_cordz_functions.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_cordz_info.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_strings.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_strings_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_cordz_handle.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_cord_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_str_format_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/strings/libabsl_cord.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/hash/libabsl_hash.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/hash/libabsl_low_level_hash.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/hash/libabsl_city.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/time/libabsl_time.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/time/libabsl_civil_time.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/time/libabsl_time_zone.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/profiling/libabsl_exponential_biased.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/types/libabsl_bad_optional_access.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/types/libabsl_bad_variant_access.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_randen_hwaes.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_randen.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_randen_slow.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_distributions.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_seed_material.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_platform.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_randen_hwaes_impl.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_internal_pool_urbg.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_seed_sequences.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/random/libabsl_random_seed_gen_exception.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/synchronization/libabsl_graphcycles_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/synchronization/libabsl_synchronization.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/container/libabsl_raw_hash_set.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/container/libabsl_hashtablez_sampler.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/debugging/libabsl_symbolize.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/debugging/libabsl_stacktrace.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/debugging/libabsl_debugging_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/debugging/libabsl_demangle_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_malloc_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_throw_delegate.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_base.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_raw_logging_internal.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_strerror.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_log_severity.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/base/libabsl_spinlock_wait.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/numeric/libabsl_int128.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/status/libabsl_statusor.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/abseil-cpp/absl/status/libabsl_status.a
            ${FIREBASE_SDK_LIBDIR}/external/src/protobuf/libprotoc.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/protobuf/libprotobuf.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/third_party/cares/cares/lib/libcares.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libgpr.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libgrpc.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libgrpc++.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/grpc-build/libaddress_sorting.a
            ${FIREBASE_SDK_LIBDIR}/external/src/firestore-build/external/src/snappy-build/libsnappy.a
            ${FIREBASE_SDK_LIBDIR}/external/src/flatbuffers-build/libflatbuffers.a
            ${FIREBASE_SDK_LIBDIR}/external/src/curl-build/lib/libcurl.a
            ${FIREBASE_SDK_LIBDIR}/external/src/boringssl/libssl.a
            ${FIREBASE_SDK_LIBDIR}/external/src/boringssl/libcrypto.a
            ${FIREBASE_SDK_LIBDIR}/external/src/zlib-build/libz.a
            PkgConfig::UUID
            PkgConfig::SECRET
            Threads::Threads
    )
endmacro()

set(PLUGIN_DATA_SOURCES
    firebase_core_plugin.cc
    firebase_core_plugin_c_api.cc
    messages.g.cc
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE INTERNAL_EXPERIMENTAL)
    target_include_directories(${PLUGIN_NAME} PUBLIC .)
    target_link_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
endmacro()

set(PLUGIN_DATA_LIBRARIES
    firebase_sdk
)
