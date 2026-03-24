find_package(gRPC CONFIG REQUIRED)
add_library(proto-objects OBJECT sample.proto)

set(output ${CMAKE_CURRENT_BINARY_DIR}/protobuf-generated)
file(MAKE_DIRECTORY ${output})

protobuf_generate(TARGET proto-objects PROTOC_OUT_DIR ${output})

protobuf_generate(
        TARGET proto-objects
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
        PROTOC_OUT_DIR ${output}
)

target_include_directories(proto-objects PUBLIC $<BUILD_INTERFACE:${output}>)
target_link_libraries(proto-objects PUBLIC protobuf::libprotobuf gRPC::grpc++)

add_executable(asyncio_grpc_client client.cpp)
target_link_libraries(asyncio_grpc_client PRIVATE asyncio-main proto-objects)

add_executable(asyncio_grpc_server server.cpp)
target_link_libraries(asyncio_grpc_server PRIVATE asyncio-main proto-objects)
