cmake_minimum_required(VERSION 3.17)
project(asyncio)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(ASYNCIO_VERSION 1.2.0)

option(BUILD_SAMPLES "Build asyncio samples" ON)
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(ASYNCIO_EMBED_CA_CERT "Use built-in CA certificates instead of system certificates" OFF)
option(ASYNCIO_ENABLE_STACKTRACE "Enable async call stack recording" ON)
option(ASYNCIO_ENABLE_C_ARES "Use c-ares for DNS resolution, enabling custom DNS server configuration" OFF)

include(CMakeDependentOption)
cmake_dependent_option(BUILD_INTEGRATION_SAMPLES "Build asyncio integration samples" OFF BUILD_SAMPLES OFF)

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

if (ASYNCIO_ENABLE_C_ARES)
    find_package(c-ares CONFIG REQUIRED)
endif ()

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
    set(ca_cert_pem ${CMAKE_CURRENT_BINARY_DIR}/cacert.pem)
    set(ca_cert_header_directory ${CMAKE_CURRENT_BINARY_DIR}/ca-cert)
    set(ca_cert_header_file ${ca_cert_header_directory}/ca_cert.h)

    if (NOT EXISTS ${ca_cert_pem})
        file(DOWNLOAD "https://curl.se/ca/cacert.pem" ${ca_cert_pem} STATUS download_status)
        list(GET download_status 0 status_code)
        if (NOT status_code EQUAL 0)
            list(GET download_status 1 error_message)
            message(FATAL_ERROR "Failed to download CA cert: ${error_message}")
        endif ()
    endif ()

    if (NOT EXISTS ${ca_cert_header_file})
        file(READ ${ca_cert_pem} content)
        string(REPLACE "\n" "\\n\"\n\"" content "${content}")
        file(WRITE ${ca_cert_header_file} "constexpr std::string_view CACert = \"${content}\";")
    endif ()

    target_compile_definitions(asyncio PUBLIC ASYNCIO_EMBED_CA_CERT)
    target_include_directories(asyncio PRIVATE ${ca_cert_header_directory})
endif ()

if (ASYNCIO_ENABLE_STACKTRACE)
    target_compile_definitions(asyncio PUBLIC ASYNCIO_ENABLE_STACKTRACE)
endif ()

if (ASYNCIO_ENABLE_C_ARES)
    target_compile_definitions(asyncio PUBLIC ASYNCIO_ENABLE_C_ARES)
endif ()

if (WIN32)
    target_compile_definitions(asyncio PUBLIC NOMINMAX)
endif ()

if (MINGW)
    target_compile_options(asyncio PRIVATE -Wa,-mbig-obj)
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
        $<$<PLATFORM_ID:Windows>:crypt32>
        $<$<BOOL:${ASYNCIO_ENABLE_C_ARES}>:c-ares::cares>
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
