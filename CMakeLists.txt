# ##############################################################################
# Required CMake version            #
# ##############################################################################
cmake_minimum_required(VERSION 3.13..3.13)

# ##############################################################################
# Project name                        #
# ##############################################################################
project(NS3 CXX C)

file(STRINGS VERSION NS3_VER)

# minimum compiler versions
set(AppleClang_MinVersion 13.1.6)
set(Clang_MinVersion 11.0.0)
set(GNU_MinVersion 10.1.0)

# common options
option(NS3_ASSERT "Enable assert on failure" OFF)
option(NS3_DES_METRICS "Enable DES Metrics event collection" OFF)
option(NS3_EXAMPLES "Enable examples to be built" OFF)
option(NS3_LOG "Enable logging to be built" OFF)
option(NS3_TESTS "Enable tests to be built" OFF)

# fd-net-device options
option(NS3_EMU "Build with emulation support" ON)
option(NS3_TAP "Build with Tap support" ON)
option(NS3_DPDK "Enable fd-net-device DPDK features" OFF)

# maintenance and documentation
option(NS3_CLANG_FORMAT "Enforce cody style with clang-format" OFF)
option(NS3_CLANG_TIDY "Use clang-tidy static analysis" OFF)
option(NS3_CLANG_TIMETRACE
       "Collect compilation statistics to analyze the build process" OFF
)
option(NS3_COLORED_OUTPUT "Colorize CMake messages" OFF)
option(NS3_COVERAGE "Enable code coverage measurements and report generation"
       OFF
)
option(NS3_COVERAGE_ZERO_COUNTERS
       "Zero lcov counters before running. Requires NS3_COVERAGE=ON" OFF
)
option(NS3_INCLUDE_WHAT_YOU_USE
       "Use IWYU to determine unnecessary headers included" OFF
)
option(NS3_LINK_WHAT_YOU_USE
       "Use LWYU to determine unnecessary linked libraries" OFF
)
option(NS3_SANITIZE "Build with address, leak and undefined sanitizers" OFF)
option(NS3_SANITIZE_MEMORY "Build with memory sanitizer" OFF)

# 3rd-party libraries/programs
option(NS3_NETANIM "Build netanim" OFF)

# other options
option(NS3_ENABLE_BUILD_VERSION "Embed version info into libraries" OFF)
option(NS3_CCACHE "Use Ccache to speed up recompilation" ON)
option(NS3_CPM "Enable the CPM C++ library manager support" OFF)
option(NS3_FAST_LINKERS "Use Mold or LLD to speed up linking if available" ON)
option(NS3_FETCH_OPTIONAL_COMPONENTS
       "Fetch Brite, Click and Openflow dependencies" OFF
)
option(
  NS3_FORCE_LOCAL_DEPENDENCIES
  "Only search applications, headers, libraries on explicitly set directories"
  OFF
)
option(NS3_GSL "Build with GSL support" ON)
option(NS3_GTK3 "Build with GTK3 support" ON)
option(NS3_LINK_TIME_OPTIMIZATION "Build with link-time optimization" OFF)
option(NS3_MONOLIB
       "Build a single shared ns-3 library and link it against executables" OFF
)
option(NS3_MPI "Build with MPI support" OFF)
option(NS3_MTP "Build with Multithreaded simulation support" OFF)
option(NS3_NATIVE_OPTIMIZATIONS "Build with -march=native -mtune=native" OFF)
option(
  NS3_NINJA_TRACING
  "Use ninjatracing to convert Ninja's build log to the about://tracing format"
  OFF
)
set(NS3_OUTPUT_DIRECTORY "" CACHE STRING "Directory to store built artifacts")
option(NS3_PRECOMPILE_HEADERS
       "Precompile module headers to speed up compilation" ON
)
option(NS3_PYTHON_BINDINGS "Build ns-3 python bindings" OFF)
option(NS3_SQLITE "Build with SQLite support" ON)
option(NS3_EIGEN "Build with Eigen support" ON)
option(NS3_STATIC "Build a static ns-3 library and link it against executables"
       OFF
)
option(NS3_VCPKG "Enable the Vcpkg C++ library manager support" OFF)
option(NS3_VERBOSE "Print additional build system messages" OFF)
option(NS3_VISUALIZER "Build visualizer module" ON)
option(NS3_WARNINGS "Enable compiler warnings" ON)
option(NS3_WARNINGS_AS_ERRORS
       "Treat warnings as errors. Requires NS3_WARNINGS=ON" ON
)

# Options that either select which modules will get built or disable modules
set(NS3_ENABLED_MODULES ""
    CACHE STRING "List of modules to enable (e.g. core;network;internet)"
)
set(NS3_DISABLED_MODULES "" CACHE STRING
                                  "List of modules to disable (e.g. lte;wimax)"
)

# Filter in the modules from which examples and tests will be built
set(NS3_FILTER_MODULE_EXAMPLES_AND_TESTS
    ""
    CACHE
      STRING
      "List of modules that should have their examples and tests built (e.g. lte;wifi)"
)

# Include macros used below
include(build-support/macros-and-definitions.cmake)

# Scan module libraries
subdirlist(libs_to_build ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Scan contribution libraries
subdirlist(contrib_libs_to_build ${CMAKE_CURRENT_SOURCE_DIR}/contrib)

# Scan for additional contribution libraries OUTSIDE the ns-3 source directory,
# in the ns-3-external-contrib directory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../ns-3-external-contrib)
  subdirlist(
    external_contrib_libs_to_build
    ${CMAKE_CURRENT_SOURCE_DIR}/../ns-3-external-contrib
  )
  set(contrib_libs_to_build ${contrib_libs_to_build}
                            ${external_contrib_libs_to_build}
  )
endif()

# Before filtering, we need to load settings from .ns3rc
parse_ns3rc(
  ns3rc_enabled_modules ns3rc_disabled_modules ns3rc_examples_enabled
  ns3rc_tests_enabled
)

# After scanning modules, we can filter enabled and disabled ones
filter_enabled_and_disabled_modules(
  libs_to_build contrib_libs_to_build NS3_ENABLED_MODULES NS3_DISABLED_MODULES
  ns3rc_enabled_modules ns3rc_disabled_modules
)

if(${NS3_CCACHE})
  # Use ccache if available
  mark_as_advanced(CCACHE)
  find_program(CCACHE ccache)
  if(NOT ("${CCACHE}" STREQUAL "CCACHE-NOTFOUND"))
    message(STATUS "CCache is enabled.")
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
    execute_process(
      COMMAND
        ${CCACHE} --set-config
        sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
    )
    if(DEFINED MSVC)
      if(MSVC)
        file(COPY_FILE ${CCACHE} ${CMAKE_BINARY_DIR}/clang-cl.exe
             ONLY_IF_DIFFERENT
        )
        set(CMAKE_VS_GLOBALS
            "CLToolExe=clang-cl.exe" "CLToolPath=${CMAKE_BINARY_DIR}"
            "TrackFileAccess=false" "UseMultiToolTask=true"
            "DebugInformationFormat=OldStyle"
        )
      endif()
    endif()
  endif()
endif()

# ##############################################################################
# Process options                     #
# ##############################################################################
process_options()

# ##############################################################################
# Add subdirectories                  #
# ##############################################################################
# Build NS3 library core
add_subdirectory(src)

# Build NS library examples
add_subdirectory(examples)

# Build scratch/simulation scripts
add_subdirectory(scratch)

# Build test utils
add_subdirectory(utils)

write_lock()
write_configtable()

# Export package targets when installing
ns3_cmake_package()
