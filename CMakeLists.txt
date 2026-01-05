cmake_minimum_required(VERSION 3.17)
project(asyncio)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(ASYNCIO_VERSION 1.0.6)

option(BUILD_SAMPLES "Build asyncio samples" ON)
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(ASYNCIO_EMBED_CA_CERT "Use built-in CA certificates instead of system certificates" OFF)

if (BUILD_SHARED_LIBS AND MSVC)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif ()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(zero CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(
        asyncio
        src/uv.cpp
        src/io.cpp
        src/fs.cpp
        src/time.cpp
        src/poll.cpp
        src/pipe.cpp
        src/thread.cpp
        src/signal.cpp
        src/stream.cpp
        src/buffer.cpp
        src/channel.cpp
        src/task.cpp
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
        $<$<PLATFORM_ID:Windows,Darwin,Linux,Android>:src/process.cpp>
)

if (ASYNCIO_EMBED_CA_CERT)
    set(ca_cert_file ${CMAKE_CURRENT_BINARY_DIR}/cacert.pem)
    set(ca_cert_header ${CMAKE_CURRENT_SOURCE_DIR}/include/ca_cert.h)

    if (NOT EXISTS ${ca_cert_file})
        file(DOWNLOAD "https://curl.se/ca/cacert.pem" ${ca_cert_file})
    endif ()

    if (NOT EXISTS ${ca_cert_header})
        file(READ ${ca_cert_file} content)
        file(WRITE ${ca_cert_header} "constexpr std::string_view CA_CERT = R\"(${content})\";")
    endif ()

    set(EXCLUDE_HEADERS ${EXCLUDE_HEADERS} PATTERN "ca_cert.h" EXCLUDE)
    target_compile_definitions(asyncio PUBLIC ASYNCIO_EMBED_CA_CERT)
endif ()

if (WIN32)
    target_compile_definitions(asyncio PUBLIC NOMINMAX)
endif ()

target_include_directories(
        asyncio
        PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
        asyncio
        PRIVATE
        $<$<PLATFORM_ID:Windows>:Crypt32>
        PUBLIC
        zero::zero
        ZLIB::ZLIB
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        nlohmann_json::nlohmann_json
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
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
        TARGETS asyncio-main
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        EXPORT ${PROJECT_NAME}Targets
        NAMESPACE ${PROJECT_NAME}::
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
