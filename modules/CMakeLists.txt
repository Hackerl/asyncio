
add_library(asyncio_module)

target_sources(asyncio_module
    PUBLIC
        FILE_SET CXX_MODULES FILES
            asyncio.cppm
            asyncio.http.cppm
            asyncio.net.cppm
            asyncio.sync.cppm
)

target_compile_features(asyncio_module PUBLIC cxx_std_20)

target_include_directories(asyncio_module PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(asyncio::module ALIAS asyncio_module)

# Installation
install(TARGETS asyncio_module
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asyncio/modules
)