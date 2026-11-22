cmake_minimum_required(VERSION 3.22.1)
project(nana_samp_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable SA-MP 0.3.7 & 0.3.DL-R1 / open.mp networking library (Linux CI + Android NDK)
add_library(nana_net STATIC
    net/bitstream.cpp
    net/string_compressor.cpp
    net/samp_crypto.cpp
    net/raknet_peer.cpp
    net/samp_query.cpp
    net/samp_client.cpp
)
target_include_directories(nana_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(nana_net PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(ANDROID)
    # Clean independent Social Club shim satisfying all 24 libGTASA.so DT_NEEDED symbols
    add_library(SCAnd SHARED
        scand_stub/scand_stub.cpp
    )
    target_compile_options(SCAnd PRIVATE -Wall -Wextra)

    # Clean independent Fastman92 plugin loader bridge forwarding PluginLoader_OnLoad -> Original_JNI_OnLoad
    add_library(Fastman92pluginLoader SHARED
        scand_stub/fastman92_stub.cpp
    )
    target_compile_options(Fastman92pluginLoader PRIVATE -Wall -Wextra)
    target_link_libraries(Fastman92pluginLoader PRIVATE log dl)

    # Nana SAMP native multiplayer & GTA SA 2.00 + AML 1.4.0 integration library
    add_library(nana_samp SHARED
        game/arm_hook.cpp
        game/aml_compat.cpp
        game/file_compat.cpp
        game/gpu_compat.cpp
        game/gtasa_symbols.cpp
        game/gtasa_world.cpp
        core/crash_handler.cpp
        core/jni_bridge.cpp
    )
    target_include_directories(nana_samp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(nana_samp PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(nana_samp PRIVATE nana_net log dl android GLESv2)
else()
    enable_testing()

    add_executable(test_net_protocol
        tests/test_net_protocol.cpp
    )
    target_link_libraries(test_net_protocol PRIVATE nana_net)
    add_test(NAME NanaNetProtocolTests COMMAND test_net_protocol)

    add_executable(test_gpu_compat
        tests/test_gpu_compat.cpp
        game/gpu_compat.cpp
        game/arm_hook.cpp
    )
    target_include_directories(test_gpu_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_gpu_compat PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME NanaGpuCompatTests COMMAND test_gpu_compat)

    add_executable(test_file_compat
        tests/test_file_compat.cpp
        game/file_compat.cpp
        game/arm_hook.cpp
        core/crash_handler.cpp
    )
    target_include_directories(test_file_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_file_compat PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME NanaFileCompatTests COMMAND test_file_compat)

    add_executable(test_openmp_integration
        tests/test_openmp_integration.cpp
    )
    target_link_libraries(test_openmp_integration PRIVATE nana_net pthread)
endif()
