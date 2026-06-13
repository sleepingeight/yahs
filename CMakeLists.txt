cmake_minimum_required(VERSION 3.28)
project(
    yahs 
    VERSION 0.1 
    LANGUAGES C CXX
    DESCRIPTION "io-uring based static webserver"
)

# Export & symlink compile commands for language servers/LSPs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(UNIX)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
    )
endif()

# Check whether io-uring is supported by this machine
if(NOT LINUX)
    message(FATAL_ERROR "io-uring only exists on linux")
else()
    message(STATUS "Performing Test HAVE_IOURING")
    set(
        IOURING_TEST
        "
    #include <linux/io_uring.h>

    int main() {
        struct io_uring_params p;
        return 0;
    }    
        "
    )

    try_compile(
        HAVE_IOURING 
        SOURCE_FROM_VAR "test_iouring.c" IOURING_TEST
    )
    if(NOT HAS_IOURING)
        message(FATAL_ERROR "io-uring is not supported by this machine")
    endif()
    message(STATUS "Performing Test HAVE_IOURING - Success")
endif()

option(
    SYSTEM_LIBURING 
    "Option to use system-wide installed liburing. [Default = OFF]"
    OFF
)

find_package(Threads REQUIRED)

if(SYSTEM_LIBURING) 
    message(STATUS "Using pkg-config to check whether liburing exists or not")
    
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing)
    
    add_library(liburing::liburing ALIAS PkgConfig::liburing)
else()
    # Fetch and build liburing
    include(ExternalProject)
    set(LIBURING_INSTALL_DIR "${CMAKE_BINARY_DIR}/liburing_install")
    
    # Creating dummy include directory at configure time to prevent CMake errors
    file(MAKE_DIRECTORY "${LIBURING_INSTALL_DIR}/include")
    
    set(LIBURING_STATIC_LIB "${LIBURING_INSTALL_DIR}/lib/liburing.a")
    
    ExternalProject_Add(liburing_project
        GIT_REPOSITORY    "https://github.com/axboe/liburing.git"
        GIT_TAG           "liburing-2.14"
        PREFIX            "${CMAKE_BINARY_DIR}/liburing"
        BUILD_IN_SOURCE   1
        CONFIGURE_COMMAND ./configure --prefix=${LIBURING_INSTALL_DIR} --libdir=${LIBURING_INSTALL_DIR}/lib --libdevdir=${LIBURING_INSTALL_DIR}/lib --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
        BUILD_COMMAND     make -j5
        INSTALL_COMMAND   make install
        BUILD_BYPRODUCTS  "${LIBURING_STATIC_LIB}"
    )    
    
    add_library(liburing::liburing STATIC IMPORTED GLOBAL)
    set_target_properties(liburing::liburing PROPERTIES
        IMPORTED_LOCATION "${LIBURING_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_INSTALL_DIR}/include"
    )
   add_dependencies(liburing::liburing liburing_project)
endif()


add_executable(yahs main.cpp)

target_link_libraries(yahs PRIVATE
    liburing::liburing
    Threads::Threads
)
