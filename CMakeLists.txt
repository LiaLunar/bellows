cmake_minimum_required(VERSION 3.28)
# version.txt holds the version; release-please bumps it together with plugin/plugin.toml.
file(STRINGS version.txt SB_VERSION_TEXT LIMIT_COUNT 1 REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT SB_VERSION_TEXT)
    message(FATAL_ERROR "version.txt must hold a MAJOR.MINOR.PATCH version")
endif()
file(STRINGS plugin/plugin.toml SB_MANIFEST_VERSION LIMIT_COUNT 1 REGEX "^version = \"[^\"]*\"$")
if(NOT SB_MANIFEST_VERSION STREQUAL "version = \"${SB_VERSION_TEXT}\"")
    message(FATAL_ERROR "plugin/plugin.toml (${SB_MANIFEST_VERSION}) does not match version.txt (${SB_VERSION_TEXT})")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt plugin/plugin.toml)
project(bellows VERSION ${SB_VERSION_TEXT} LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SB_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG v3.5.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus mimalloc)

# zlib for save_compress's parallel compression (the game's own zlib does the rest). Only the sources:
# SOURCE_SUBDIR without a CMakeLists.txt, so zlib's own project is not added.
FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(zlib)
function(sb_add_zlib target dir)
    add_library(${target} STATIC
        ${dir}/adler32.c ${dir}/compress.c ${dir}/crc32.c ${dir}/deflate.c ${dir}/infback.c ${dir}/inffast.c
        ${dir}/inflate.c ${dir}/inftrees.c ${dir}/trees.c ${dir}/uncompr.c ${dir}/zutil.c)
    target_include_directories(${target} PUBLIC ${dir})
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W0)
    endif()
endfunction()
sb_add_zlib(sb_zlib ${zlib_SOURCE_DIR})

# The Anvil SDK: the AnvilPlugin CMake package (anvil_add_plugin, the headers,
# anvil.exe for `anvil gen` and `anvil check`).
#   - By default the SDK archive of the Anvil release SB_ANVIL_VERSION, checked
#     against SB_ANVIL_SDK_SHA256 (the release's SHA256SUMS.txt). A newer
#     release changes both, on purpose, in its own pull request.
#   - An unpacked SDK archive, or an SDK installed from a local Anvil checkout
#     (`cmake --install <anvil build> --component sdk`): -DAnvilPlugin_DIR=<sdk>/cmake.
set(SB_ANVIL_VERSION 1.4.0)
set(SB_ANVIL_SDK_SHA256 e8c9801b1cdddf4eecacbf02eae121fb03734674073b84089820493b24a2a4a3)
if(AnvilPlugin_DIR)
    find_package(AnvilPlugin REQUIRED)
else()
    FetchContent_Declare(anvil_sdk
        URL https://github.com/happy-dwarf-team/anvil/releases/download/v${SB_ANVIL_VERSION}/anvil-sdk-windows-x64.zip
        URL_HASH SHA256=${SB_ANVIL_SDK_SHA256})
    FetchContent_MakeAvailable(anvil_sdk)
endif()

# bellows.dll and its folder <build>/plugin/bellows/ (plugin.toml, symbols.toml,
# settings.toml, the DLL), laid out by the package; `anvil gen` writes
# symbols.gen.hpp (sym::) and settings.gen.hpp (cfg::) at build time.
set(SB_SOURCES
    src/benchmark.cpp
    src/common.cpp
    src/rename.cpp
    src/settings.cpp
    src/modules/access_profile.cpp
    src/modules/attr_cache.cpp
    src/modules/zip_profile.cpp
    src/modules/zip_reader.cpp
    src/modules/ram_prefetch.cpp
    src/modules/exists_index.cpp
    src/modules/metascript_subst.cpp
    src/modules/metascript_blocks.cpp
    src/modules/mimalloc_heap.cpp
    src/modules/open_warmup.cpp
    src/modules/token_dict_cache.cpp
    src/modules/token_dict_profile.cpp
    src/modules/range_profile.cpp
    src/modules/range_witness.cpp
    src/modules/frame_pacing.cpp
    src/modules/modifier_merge.cpp
    src/modules/province_modifier_view.cpp
    src/modules/save_compress.cpp
    src/modules/save_compress_parallel.cpp
    src/modules/tick_profile.cpp
    src/modules/army_search_profile.cpp
    src/modules/score_probe.cpp
    src/modules/monthly_probe.cpp
    src/modules/trigger_intern.cpp
    src/modules/verifypath_listing.cpp)
# File version shown in the DLL's Properties, from the same version.txt.
configure_file(src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc @ONLY)
anvil_add_plugin(bellows PLUGIN_DIR plugin
    SOURCES src/plugin.cpp ${SB_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc)
target_include_directories(bellows PRIVATE src)
target_link_libraries(bellows PRIVATE tomlplusplus::tomlplusplus mimalloc-static sb_zlib shell32 ole32 uuid)
target_compile_definitions(bellows PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(bellows PRIVATE /EHsc)
    # Keep every module's exported counters even though nothing in the DLL references them.
    target_link_options(bellows PRIVATE /OPT:NOREF)
endif()
# The shipped profiles next to the DLL, as in the release package.
add_custom_command(TARGET bellows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/plugin/profiles
        ${CMAKE_BINARY_DIR}/plugin/bellows/profiles
    VERBATIM)

# anvil.exe of the SDK, for CI (`anvil check --format github`): its path in
# <build>/anvil-tool-<config>.txt.
get_property(SB_ANVIL_TOOL GLOBAL PROPERTY ANVIL_SDK_TOOL)
if(TARGET ${SB_ANVIL_TOOL})
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/anvil-tool-$<CONFIG>.txt CONTENT "$<TARGET_FILE:${SB_ANVIL_TOOL}>")
else()
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/anvil-tool-$<CONFIG>.txt CONTENT "${SB_ANVIL_TOOL}")
endif()

if(SB_BUILD_TESTS)
    enable_testing()
    foreach(test rename trigger_intern token_dict_cache metascript_subst metascript_blocks settings access_profile path_rules zip_reader range_stats range_witness tick_profile army_search score_probe monthly_probe modifier_merge province_modifier_view frame_pacing)
        add_executable(test_${test} tests/test_${test}.cpp src/benchmark.cpp src/common.cpp src/settings.cpp src/rename.cpp
            src/modules/access_profile.cpp src/modules/zip_reader.cpp)
        # The SDK headers and the generated symbols.gen.hpp / settings.gen.hpp of the plugin.
        target_include_directories(test_${test} PRIVATE src $<TARGET_PROPERTY:bellows,INCLUDE_DIRECTORIES>)
        add_dependencies(test_${test} bellows)
        target_link_libraries(test_${test} PRIVATE tomlplusplus::tomlplusplus shell32 ole32 uuid)
        target_compile_definitions(test_${test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
            SB_SETTINGS_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/plugin/settings.toml")
        if(MSVC)
            # /UNDEBUG: the tests use assert(), which Release would compile out.
            target_compile_options(test_${test} PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
        endif()
        add_test(NAME ${test} COMMAND test_${test})
    endforeach()
    # The mimalloc heap functions without the import slots (SB_MIMALLOC_TEST: InitMimallocForTest).
    add_executable(test_mimalloc_heap tests/test_mimalloc_heap.cpp src/modules/mimalloc_heap.cpp src/benchmark.cpp
        src/common.cpp src/settings.cpp)
    target_include_directories(test_mimalloc_heap PRIVATE src $<TARGET_PROPERTY:bellows,INCLUDE_DIRECTORIES>)
    add_dependencies(test_mimalloc_heap bellows)
    target_link_libraries(test_mimalloc_heap PRIVATE tomlplusplus::tomlplusplus mimalloc-static shell32 ole32 uuid)
    target_compile_definitions(test_mimalloc_heap PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX SB_MIMALLOC_TEST)
    if(MSVC)
        target_compile_options(test_mimalloc_heap PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
    endif()
    add_test(NAME mimalloc_heap COMMAND test_mimalloc_heap)
    # save_compress: the CRC, the game's 0x153C790 written out and the parallel compression, with Bellows' zlib.
    add_executable(test_save_compress tests/test_save_compress.cpp src/modules/save_compress_parallel.cpp)
    target_include_directories(test_save_compress PRIVATE src)
    target_link_libraries(test_save_compress PRIVATE sb_zlib)
    if(MSVC)
        target_compile_options(test_save_compress PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
    endif()
    add_test(NAME save_compress COMMAND test_save_compress)
    # bellows.dll's anvil_plugin_load against a stand-in host (tests/test_plugin_load.cpp).
    add_executable(test_plugin_load tests/test_plugin_load.cpp)
    target_include_directories(test_plugin_load PRIVATE $<TARGET_PROPERTY:bellows,INCLUDE_DIRECTORIES>)
    add_dependencies(test_plugin_load bellows)
    if(MSVC)
        target_compile_options(test_plugin_load PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
    endif()
    foreach(scenario ok refuse old)
        add_test(NAME plugin_load_${scenario}
            COMMAND test_plugin_load $<TARGET_FILE:bellows> ${CMAKE_BINARY_DIR}/plugin/bellows ${scenario})
    endforeach()
    # plugin/symbols.lock matches plugin/symbols.toml and the last `anvil check --exe`
    # on the real eu4.exe was clean; also checks plugin.toml and settings.toml.
    add_test(NAME symbols_lock COMMAND ${SB_ANVIL_TOOL} check ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
endif()

# Offline checks of save_compress on compressed saves (tools/save_compress_offline.cpp): with zlib 1.2.8,
# the game's version, CompressEntry must give the bytes the game wrote; with Bellows' zlib, the parallel
# compression must decode to the same member. Off by default: it downloads zlib 1.2.8 too.
option(SB_BUILD_SAVE_TOOLS "Build the offline save compression checks" OFF)
if(SB_BUILD_SAVE_TOOLS)
    FetchContent_Declare(zlib128
        URL https://github.com/madler/zlib/archive/refs/tags/v1.2.8.tar.gz
        URL_HASH SHA256=e380bd1bdb6447508beaa50efc653fe45f4edc1dafe11a251ae093e0ee97db9a
        SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(zlib128)
    sb_add_zlib(sb_zlib128 ${zlib128_SOURCE_DIR})
    add_executable(save_compress_check_game tools/save_compress_offline.cpp)
    target_include_directories(save_compress_check_game PRIVATE src)
    target_compile_definitions(save_compress_check_game PRIVATE SB_TOOL_GAME_ZLIB WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(save_compress_check_game PRIVATE sb_zlib128)
    add_executable(save_compress_check tools/save_compress_offline.cpp src/modules/save_compress_parallel.cpp)
    target_include_directories(save_compress_check PRIVATE src)
    target_compile_definitions(save_compress_check PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(save_compress_check PRIVATE sb_zlib)
    if(MSVC)
        target_compile_options(save_compress_check_game PRIVATE /utf-8 /permissive- /EHsc)
        target_compile_options(save_compress_check PRIVATE /utf-8 /permissive- /EHsc)
    endif()
endif()

# Plugin folder layout: <prefix>/bellows/{plugin.toml, symbols.toml, settings.toml, bellows.dll, profiles/, LICENSE}
install(TARGETS bellows RUNTIME DESTINATION bellows COMPONENT plugin)
install(FILES plugin/plugin.toml plugin/symbols.toml plugin/settings.toml LICENSE DESTINATION bellows COMPONENT plugin)
install(DIRECTORY plugin/profiles DESTINATION bellows COMPONENT plugin)
# Third-party code compiled into the DLL (MIT): ship its license texts.
install(FILES ${mimalloc_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME mimalloc-LICENSE.txt COMPONENT plugin)
install(FILES ${tomlplusplus_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME tomlplusplus-LICENSE.txt COMPONENT plugin)
install(FILES ${zlib_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME zlib-LICENSE.txt COMPONENT plugin)
