cmake_minimum_required(VERSION 3.14) # This is a guess
project(DrMd VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

if(WIN32)
add_compile_options("-D_CRT_NONSTDC_NO_DEPRECATE" "-D_CRT_SECURE_NO_WARNINGS")
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
add_compile_options("/wd4244" "/wd5105" "/wd4146" "/wd4267" "/wd4018" "/wd4141" "/wd4163")
endif()
elseif(APPLE)
elseif(UNIX)
set(LIBM_LIBRARIES m)
endif()

add_executable(drmd drmd_cli.c)

install(TARGETS drmd DESTINATION bin)

add_executable(test-drmd TestDrMd.c)

enable_testing()
add_test(test-drmd test-drmd)
