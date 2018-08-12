cmake_minimum_required(VERSION 3.5)
project(irods_smb)

find_package(IRODS 4.3.0 EXACT REQUIRED CONFIG)

set(CMAKE_C_COMPILER ${IRODS_EXTERNALS_FULLPATH_CLANG}/bin/clang)
set(CMAKE_CXX_COMPILER ${IRODS_EXTERNALS_FULLPATH_CLANG}/bin/clang++)

include(${IRODS_TARGETS_PATH})

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH ${IRODS_EXTERNALS_FULLPATH_CLANG_RUNTIME}/lib)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")

add_library(${PROJECT_NAME} SHARED libirods_smb.cpp)
target_compile_options(${PROJECT_NAME} PRIVATE -std=c++17 -Wall -Wextra)
target_compile_definitions(${PROJECT_NAME} PRIVATE ${IRODS_COMPILE_DEFINITIONS})
target_include_directories(${PROJECT_NAME} PRIVATE ${IRODS_INCLUDE_DIRS}
                                                   ${IRODS_EXTERNALS_FULLPATH_CLANG}/include/c++/v1
                                                   ${IRODS_EXTERNALS_FULLPATH_BOOST}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE c++abi
                                              irods_client
                                              irods_plugin_dependencies
                                              irods_common
                                              /usr/lib/irods/plugins/network/libtcp_client.so
                                              ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_filesystem.so
                                              ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so)

# Does not compile with -std=cXX
add_executable(libtest libtest.c)
target_compile_options(libtest PRIVATE -std=gnu11 -Wall -Wextra)
target_link_libraries(libtest PRIVATE ${PROJECT_NAME})

