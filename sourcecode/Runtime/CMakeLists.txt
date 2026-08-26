cmake_minimum_required(VERSION 3.16)
project(nom)

### RUN CONFIG
#   the valid version of clang and llvm is 12
#   sepcify the CMAKE_C_COMPILER and CMAKE_CXX_COMPILER to clang-12
#   set LLVM_DIR to {your-llvm-12-path}/lib/cmake/llvm if you have multiple versions of llvm 

# Set the C and C++ compilers
# Set Manually in command line
# SET(CMAKE_C_COMPILER clang-12)
# SET(CMAKE_CXX_COMPILER clang++-12)
# Set the C and C++ standards
SET(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# export compile commands
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Intel VTune Profiler and TBB paths
SET(VTUNE_PATH /opt/intel/oneapi/vtune/latest)
SET(TBB_PATH /opt/intel/oneapi/tbb/latest)
# Generator
set(CMAKE_GENERATOR "Unix Makefiles")
message(STATUS "Generator is set to ${CMAKE_GENERATOR}")
# Set the output directory for runtime executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")

# Set compile options based on build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "No cmake build type specified, defaulting to Release")
    add_compile_options(-O3)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "Cmake build type set to Release")
    add_compile_options(-O3)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_BUILD_TYPE Debug)
    message(STATUS "Cmake build type set to Debug")
    add_compile_options(-O0)
else()
    message(FATAL_ERROR "Invalid cmake build type specified: ${CMAKE_BUILD_TYPE}")
endif()

if (NOT NOM_BUILD_TYPE)
    set(NOM_BUILD_TYPE Static)
    message(STATUS "No nom build type specified, defaulting to Static")
elseif (NOM_BUILD_TYPE STREQUAL "Static")
    message(STATUS "Nom build type set to Static")
elseif (NOM_BUILD_TYPE STREQUAL "Shared")
    message(STATUS "Nom build type set to Shared")
else()
    message(FATAL_ERROR "Invalid nom build type specified: ${NOM_BUILD_TYPE}")
endif()

add_compile_options(-fexceptions -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS
                    -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

add_link_options(-Xlinker --export-dynamic)

# LLVM check exists
find_package(LLVM 12 REQUIRED)
if (LLVM_FOUND)
    message(STATUS "Found LLVM: ${LLVM_PACKAGE_VERSION}")
else()
    message(FATAL_ERROR "LLVM not found!")
endif()

# Intel VTune Profiler check exist
if (EXISTS ${VTUNE_PATH}/bin64/vtune)
    file(REAL_PATH ${VTUNE_PATH} VTUNE_REAL_PATH)
    string(REGEX MATCH "[0-9]+\\.[0-9]+" VTUNE_VERSION ${VTUNE_REAL_PATH})
    message(STATUS "Found Intel VTune Profiler version: ${VTUNE_VERSION}")
else()
    message(FATAL_ERROR "Intel VTune Profiler not found")
endif()

# libtbb check exist
find_package(TBB REQUIRED)
if (TBB_FOUND)
    message(STATUS "Found TBB: ${TBB_VERSION}")
else()
    message(FATAL_ERROR "libtbb not found!")
endif()

# libgc-dev check exist
find_library(GC_LIBRARY NAMES gc)
if (GC_LIBRARY)
    message(STATUS "Found Boehm garbage collector: ${GC_LIBRARY}")
else()
    message(FATAL_ERROR "Boehm garbage collector library not found!")
endif()

# libpugixml-dev check exist
find_package(pugixml REQUIRED CONFIG)
if(pugixml_FOUND)
    message(STATUS "Found PugiXML: ${pugixml_VERSION}")
else()
    message(FATAL_ERROR "libpugixml not found!")
endif()

# All include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Common
    ${CMAKE_SOURCE_DIR}/FileSystem
    ${CMAKE_SOURCE_DIR}/Instructions
    ${CMAKE_SOURCE_DIR}/IR
    ${CMAKE_SOURCE_DIR}/IR/Callables
    ${CMAKE_SOURCE_DIR}/IR/CallTags
    ${CMAKE_SOURCE_DIR}/IR/Data
    ${CMAKE_SOURCE_DIR}/IR/Types
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Parse
    ${CMAKE_SOURCE_DIR}/pugixml
    ${CMAKE_SOURCE_DIR}/Runtime
    ${CMAKE_SOURCE_DIR}/Runtime/Config
    ${CMAKE_SOURCE_DIR}/Runtime/Memory
    ${CMAKE_SOURCE_DIR}/Runtime/Statistics
    ${CMAKE_SOURCE_DIR}/Runtime/Typing
    ${CMAKE_SOURCE_DIR}/RuntimeData
    ${CMAKE_SOURCE_DIR}/RuntimeData/Headers
    ${CMAKE_SOURCE_DIR}/RuntimeData/Types
    ${CMAKE_SOURCE_DIR}/RuntimeData/VTables
    # checked above
    ${LLVM_INCLUDE_DIRS}
    ${VTUNE_PATH}/include
    ${TBB_PATH}/include
)

file(GLOB_RECURSE MAIN
    ${CMAKE_SOURCE_DIR}/Runtime.cpp
)

if (NOM_BUILD_TYPE STREQUAL "Static")
    file(GLOB_RECURSE CXXSRC
        ${CMAKE_SOURCE_DIR}/Common/*.cpp
        ${CMAKE_SOURCE_DIR}/FileSystem/*.cpp
        ${CMAKE_SOURCE_DIR}/Instructions/*.cpp
        ${CMAKE_SOURCE_DIR}/IR/*.cpp
        ${CMAKE_SOURCE_DIR}/Library/*.cpp
        ${CMAKE_SOURCE_DIR}/Parse/*.cpp
        ${CMAKE_SOURCE_DIR}/Runtime/*.cpp
        ${CMAKE_SOURCE_DIR}/RuntimeData/*.cpp
    )
endif()

set(EXECUTABLE ${PROJECT_NAME})

# Executable
add_executable(${EXECUTABLE} ${MAIN} ${CXXSRC})

target_link_directories(${EXECUTABLE} PRIVATE
    # checked above
    ${LLVM_LIBRARY_DIRS}
    ${VTUNE_PATH}/lib64
    ${TBB_PATH}/lib
)

target_link_libraries(${EXECUTABLE} PRIVATE
    # System Libraries
    gc
    pugixml
    # Project Libraries
    LLVM-12
    jitprofiling
    tbb
)

# Monnom Libraries
if (NOM_BUILD_TYPE STREQUAL "Shared")
    file(GLOB_RECURSE SRC_Common ${CMAKE_SOURCE_DIR}/Common/*.cpp)
    add_library(Common SHARED ${SRC_Common})

    file(GLOB_RECURSE SRC_FileSystem ${CMAKE_SOURCE_DIR}/FileSystem/*.cpp)
    add_library(FileSystem SHARED ${SRC_FileSystem})

    file(GLOB_RECURSE SRC_Instructions ${CMAKE_SOURCE_DIR}/Instructions/*.cpp)
    add_library(Instructions SHARED ${SRC_Instructions})

    file(GLOB_RECURSE SRC_IR ${CMAKE_SOURCE_DIR}/IR/*.cpp)
    add_library(IR SHARED ${SRC_IR})

    file(GLOB_RECURSE SRC_Library ${CMAKE_SOURCE_DIR}/Library/*.cpp)
    add_library(Library SHARED ${SRC_Library})

    file(GLOB_RECURSE SRC_Parse ${CMAKE_SOURCE_DIR}/Parse/*.cpp)
    add_library(Parse SHARED ${SRC_Parse})

    file(GLOB_RECURSE SRC_Runtime ${CMAKE_SOURCE_DIR}/Runtime/*.cpp)
    add_library(Runtime SHARED ${SRC_Runtime})

    file(GLOB_RECURSE SRC_RuntimeData ${CMAKE_SOURCE_DIR}/RuntimeData/*.cpp)
    add_library(RuntimeData SHARED ${SRC_RuntimeData})

    target_link_libraries(${EXECUTABLE} PRIVATE
        Common
        FileSystem
        Instructions
        IR
        Library
        Parse
        Runtime
        RuntimeData
    )
endif()
