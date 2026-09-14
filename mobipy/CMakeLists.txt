#
# This is an experimental build system for the shared library (c_abi) used by mobipy and Mobius.jl. 
#  It has been tested on Windows. Hopefully we will be able to smooth out any remaining difficulties.
#
# ---------------------------------------------------------------------------
# You have to install LLVM (version 18.x.x) and OpenXLSX yourself, then
# point cmake at their locations when you build this project using -DLLVM_DIR and -DOpenXLSX_DIR (see below).
#
#   1. LLVM 18
#        - Linux:   Install LLVM 18 from apt:
#                       wget https://apt.llvm.org/llvm.sh
#						chmod +x llvm.sh
#						./llvm.sh 18
#						rm llvm.sh
#                    (the install path is usually -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm).
#        - Windows: You can build it yourself (see https://github.com/NIVANorge/Mobius2/blob/main/HOW_TO_BUILD.md#get-llvm )
#                    A typical install directory is:
#                    -DLLVM_DIR=C:/Data/llvm-project/build/lib/cmake/llvm
#                   Alternatively, it may be possible to install it using 
#                      winget install LLVM.LLVM --version 18.1.8
#                  (untested, but maybe much faster)
#        - macOS:   `brew install llvm@18` installs it under
#                    /opt/homebrew/opt/llvm@18/lib/cmake/llvm (Apple Silicon)
#                    or /usr/local/opt/llvm@18/lib/cmake/llvm (Intel).
#
#   2. OpenXLSX — Clone from https://github.com/troldal/OpenXLSX.
#      built+installed with its own CMake (follow instructions from https://github.com/troldal/OpenXLSX#building-as-a-separate-library)
#
# Typical configure line for building this project:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release \
#         -DLLVM_DIR=<path-to-llvm-cmake-dir> \
#         -DOpenXLSX_DIR=<path-to-openxlsx-cmake-dir>
#   cmake --build build --config Release
#
# ---------------------------------------------------------------------------
 
cmake_minimum_required(VERSION 3.20)
project(mobipy_c_abi LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
 
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
 
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
 
set(MOBIPY_SOURCES
    ${SRC_DIR}/c_abi.cpp
    ${SRC_DIR}/support/resize_data_set.cpp
    ${SRC_DIR}/llvm_jit.cpp
    ${SRC_DIR}/resolve_identifier.cpp
    ${SRC_DIR}/model_compilation.cpp
    ${SRC_DIR}/model_codegen.cpp
    ${SRC_DIR}/tree_pruning.cpp
    ${SRC_DIR}/spreadsheet_inputs_openxlsx.cpp
    ${SRC_DIR}/process_series_data.cpp
    ${SRC_DIR}/data_set.cpp
    ${SRC_DIR}/model_application.cpp
    ${SRC_DIR}/model_composition.cpp
    ${SRC_DIR}/run_model.cpp
    ${SRC_DIR}/lexer.cpp
    ${SRC_DIR}/ast.cpp
    ${SRC_DIR}/model_declaration.cpp
    ${SRC_DIR}/function_tree.cpp
    ${SRC_DIR}/emulate.cpp
    ${SRC_DIR}/units.cpp
    ${SRC_DIR}/ode_solvers.cpp
    ${SRC_DIR}/file_utils.cpp
    ${SRC_DIR}/connection_regex.cpp
    ${SRC_DIR}/index_data.cpp
    ${SRC_DIR}/catalog.cpp
    ${SRC_DIR}/model_specific/nivafjord_special.cpp
    ${SRC_DIR}/model_specific/nivafjord_jetmix.cpp
    ${SRC_DIR}/model_specific/magic_special.cpp
    ${SRC_DIR}/external_computations.cpp
)
 
add_library(c_abi SHARED ${MOBIPY_SOURCES})
 
set_target_properties(c_abi PROPERTIES
    PREFIX ""
    OUTPUT_NAME "c_abi"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"   # .so (Linux/macOS)
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"   # .dll (Windows)
)

foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${_cfg} _CFG)
    set_target_properties(c_abi PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_CURRENT_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endforeach()
 
# ---------------------------------------------------------------------------
# OpenXLSX
# ---------------------------------------------------------------------------
find_package(OpenXLSX REQUIRED)
target_link_libraries(c_abi PRIVATE OpenXLSX::OpenXLSX)
 
# ---------------------------------------------------------------------------
# LLVM
# ---------------------------------------------------------------------------
# Not requesting a version here on purpose: LLVM's own LLVMConfigVersion.cmake
# compatibility check is stricter than most packages' and can reject a
# partial version like "18" even against a genuinely compatible 18.1.8 build.
# Since LLVM_DIR already points at the exact build you want, we just check
# the major version manually after the fact instead.
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} (LLVM_DIR: ${LLVM_DIR})")
if(NOT LLVM_PACKAGE_VERSION MATCHES "^18\\.")
    message(WARNING "Expected LLVM 18.x but found ${LLVM_PACKAGE_VERSION} -- "
                     "check LLVM_DIR if this isn't the build you intended.")
endif()
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
 
target_include_directories(c_abi PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(c_abi PRIVATE
    ${LLVM_DEFINITIONS_LIST}
    __STDC_CONSTANT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_LIMIT_MACROS
    MOBIUS_ERROR_STREAMS
)
 
# Component list mirrors what's needed for JIT compilation + native x86
# codegen llvm_map_components_to_libnames() picks the right static libs
# on Windows and the right shared libLLVM-18 on Linux/macOS automatically).
llvm_map_components_to_libnames(llvm_libs
    core
    support
    mcjit
    orcjit
    executionengine
    interpreter
    runtimedyld
    x86asmparser
    x86codegen
    x86desc
    x86disassembler
    x86info
    irreader
    asmparser
    bitwriter
    bitreader
    linker
    passes
    native
)
target_link_libraries(c_abi PRIVATE ${llvm_libs})
 
# ---------------------------------------------------------------------------
# Platform-specific compile / link flags
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_definitions(c_abi PRIVATE
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_NONSTDC_NO_WARNINGS
        _SCL_SECURE_NO_DEPRECATE
        _SCL_SECURE_NO_WARNINGS
    )
    target_compile_options(c_abi PRIVATE
        /EHsc     # enable C++ exceptions (matches cl /EHsc in compile_dll.bat)
        /GR-      # disable RTTI -- LLVM's own Windows builds are usually
                  # built without RTTI too; if your LLVM build has
                  # LLVM_ENABLE_RTTI=ON, drop this flag or you'll get link
                  # errors about typeinfo.
        /W0       # matches the /w (suppress warnings) in compile_dll.bat
    )
    target_link_libraries(c_abi PRIVATE
        psapi shell32 uuid advapi32 ntdll Ws2_32
    )
else()
    # Linux and macOS (clang or gcc)
    target_compile_options(c_abi PRIVATE
        -Wno-return-type
        -Wno-switch
        -fcxx-exceptions
    )
 
    if(APPLE)
        # macOS-style "resolve undefined symbols at load time" -- needed so
        # JIT'ed model code can call back into c_abi's own symbols.
        target_link_options(c_abi PRIVATE -undefined dynamic_lookup)
    else()
        target_compile_definitions(c_abi PRIVATE _GNU_SOURCE)
        target_link_options(c_abi PRIVATE
            -Wl,-undefined,dynamic_lookup
            -Wl,--export-dynamic
        )
    endif()
endif()