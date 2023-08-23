cmake_minimum_required(VERSION 3.17)
project(asyncio)

set(CMAKE_CXX_STANDARD 20)

option(ASYNCIO_DISABLE_SSL "disable ssl support" OFF)
option(ASYNCIO_DISABLE_TESTS "disable asyncio unit test" OFF)
option(ASYNCIO_DISABLE_SAMPLES "disable asyncio samples" OFF)
option(ASYNCIO_EMBED_CA_CERT "use embedded CA certificates" OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

set(ASYNCIO_VERSION 1.0.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(zero CONFIG REQUIRED)
find_package(Libevent CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(
        asyncio
        STATIC
        src/io.cpp
        src/error.cpp
        src/worker.cpp
        src/event_loop.cpp
        src/ev/pipe.cpp
        src/ev/event.cpp
        src/ev/buffer.cpp
        src/ev/signal.cpp
        src/ev/timer.cpp
        src/net/net.cpp
        src/net/dns.cpp
        src/net/dgram.cpp
        src/net/stream.cpp
        $<$<NOT:$<BOOL:${ASYNCIO_DISABLE_SSL}>>:src/net/ssl.cpp>
)

if (ASYNCIO_EMBED_CA_CERT)
    set(CA_CERT_FILE ${CMAKE_CURRENT_BINARY_DIR}/cacert.pem)
    set(CA_CERT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/cacert.h)

    if (NOT EXISTS ${CA_CERT_FILE})
        file(DOWNLOAD https://curl.se/ca/cacert.pem ${CA_CERT_FILE})
    endif ()

    if (NOT EXISTS ${CA_CERT_HEADER})
        file(READ ${CA_CERT_FILE} content HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content ${content})
        file(WRITE ${CA_CERT_HEADER} "constexpr unsigned char CA_CERT[] = {${content}};")
    endif ()

    set(EXCLUDE_HEADERS ${EXCLUDE_HEADERS} PATTERN "cacert.h" EXCLUDE)
    target_compile_definitions(asyncio PUBLIC ASYNCIO_EMBED_CA_CERT)
endif ()

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
        libevent::core
        libevent::extra
        nlohmann_json::nlohmann_json
        $<$<NOT:$<PLATFORM_ID:Windows>>:libevent::pthreads>
        $<$<NOT:$<BOOL:${ASYNCIO_DISABLE_SSL}>>:libevent::openssl>
)

if (ASYNCIO_DISABLE_SSL)
    set(
            EXCLUDE_HEADERS
            ${EXCLUDE_HEADERS}
            PATTERN "net/ssl.h" EXCLUDE
    )
endif ()

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

if (NOT ASYNCIO_DISABLE_SAMPLES)
    add_subdirectory(sample)
endif ()

if (NOT ASYNCIO_DISABLE_TESTS)
    add_subdirectory(test)
endif ()