cmake_minimum_required(VERSION 3.17)
project(asyncio)

set(CMAKE_CXX_STANDARD 23)

option(BUILD_SAMPLES "build asyncio samples" ON)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(ASYNCIO_VERSION 1.0.3)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(OpenSSL REQUIRED)
find_package(zero CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(
        asyncio
        STATIC
        src/io.cpp
        src/fs.cpp
        src/time.cpp
        src/poll.cpp
        src/pipe.cpp
        src/signal.cpp
        src/stream.cpp
        src/process.cpp
        src/event_loop.cpp
        src/net/net.cpp
        src/net/dns.cpp
        src/net/tls.cpp
        src/net/dgram.cpp
        src/net/stream.cpp
        src/http/url.cpp
        src/http/request.cpp
        src/http/websocket.cpp
        src/sync/mutex.cpp
        src/sync/event.cpp
        src/sync/condition.cpp
)

target_include_directories(
        asyncio
        PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
        asyncio
        PUBLIC
        zero::zero
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        nlohmann_json::nlohmann_json
        $<$<PLATFORM_ID:Windows>:Crypt32>
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
)

add_library(asyncio-main STATIC src/main.cpp)
target_link_libraries(asyncio-main PUBLIC asyncio)

install(
        DIRECTORY
        include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        ${EXCLUDE_HEADERS}
)

install(
        TARGETS asyncio
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        TARGETS asyncio-main
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        EXPORT ${PROJECT_NAME}Targets
        NAMESPACE asyncio::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

configure_package_config_file(
        cmake/${PROJECT_NAME}-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        VERSION ${ASYNCIO_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

if (BUILD_SAMPLES)
    add_subdirectory(sample)
endif ()

if (BUILD_TESTING)
    add_subdirectory(test)
endif ()
