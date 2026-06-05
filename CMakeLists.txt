cmake_minimum_required(VERSION 3.20)

project(KvadraAccel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
find_package(Threads REQUIRED)
find_path(KVADRA_ACCEL_BOOST_ASIO_INCLUDE_DIR boost/asio.hpp)
find_path(KVADRA_ACCEL_BOOST_ASIO_SSL_INCLUDE_DIR boost/asio/ssl.hpp)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(
    KvadraAccel_accel_lib
    src/common/config.cpp
    src/common/cli.cpp
    src/common/domain.cpp
    src/common/json_protocol.cpp
    src/common/socket.cpp
    src/tcp/level1.cpp
)

target_include_directories(KvadraAccel_accel_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(KvadraAccel_accel_lib PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(KvadraAccel_accel_lib PUBLIC cxx_std_20)

option(KVADRA_ACCEL_BUILD_ASIO_TCP "Use Boost.Asio async TCP/TLS runtime for Level 1 executables" ON)

if(KVADRA_ACCEL_BUILD_ASIO_TCP AND KVADRA_ACCEL_BOOST_ASIO_INCLUDE_DIR AND KVADRA_ACCEL_BOOST_ASIO_SSL_INCLUDE_DIR)
    find_package(OpenSSL REQUIRED)
    target_sources(KvadraAccel_accel_lib PRIVATE src/tcp/asio_level1.cpp)
    target_compile_definitions(KvadraAccel_accel_lib PUBLIC KVADRA_ACCEL_HAS_ASIO_TCP=1)
    target_link_libraries(KvadraAccel_accel_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
elseif(KVADRA_ACCEL_BUILD_ASIO_TCP)
    message(WARNING "Boost.Asio headers were not found; falling back to POSIX TCP runtime. Install libboost-dev to build async TCP/TLS.")
endif()

add_executable(KvadraAccel_server src/apps/server_main.cpp)
target_link_libraries(KvadraAccel_server PRIVATE KvadraAccel_accel_lib)

add_executable(KvadraAccel_node_a src/apps/node_a_main.cpp)
target_link_libraries(KvadraAccel_node_a PRIVATE KvadraAccel_accel_lib)

add_executable(KvadraAccel_node_b src/apps/node_b_main.cpp)
target_link_libraries(KvadraAccel_node_b PRIVATE KvadraAccel_accel_lib)

option(KVADRA_ACCEL_BUILD_GRPC "Build Level 2 gRPC executables" OFF)

if(KVADRA_ACCEL_BUILD_GRPC)
    find_package(Protobuf REQUIRED)
    find_package(gRPC REQUIRED)

    set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/accelerometer.proto)
    set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

    set(PROTO_SRCS ${PROTO_GENERATED_DIR}/accelerometer.pb.cc)
    set(PROTO_HDRS ${PROTO_GENERATED_DIR}/accelerometer.pb.h)
    set(GRPC_SRCS ${PROTO_GENERATED_DIR}/accelerometer.grpc.pb.cc)
    set(GRPC_HDRS ${PROTO_GENERATED_DIR}/accelerometer.grpc.pb.h)

    add_custom_command(
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND protobuf::protoc
        ARGS --grpc_out=${PROTO_GENERATED_DIR}
             --cpp_out=${PROTO_GENERATED_DIR}
             -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
             --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
    )

    add_library(KvadraAccel_grpc_proto ${PROTO_SRCS} ${GRPC_SRCS})
    target_include_directories(KvadraAccel_grpc_proto PUBLIC ${PROTO_GENERATED_DIR})
    target_link_libraries(KvadraAccel_grpc_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

    add_library(
        KvadraAccel_grpc_lib
        src/grpc/grpc_callback_level2.cpp
        src/grpc/grpc_level2.cpp
        src/grpc/proto_conversion.cpp
    )
    target_include_directories(KvadraAccel_grpc_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(KvadraAccel_grpc_lib PUBLIC KvadraAccel_accel_lib KvadraAccel_grpc_proto)

    add_executable(KvadraAccel_grpc_server src/apps/grpc_server_main.cpp)
    target_link_libraries(KvadraAccel_grpc_server PRIVATE KvadraAccel_grpc_lib)

    add_executable(KvadraAccel_grpc_node_a src/apps/grpc_node_a_main.cpp)
    target_link_libraries(KvadraAccel_grpc_node_a PRIVATE KvadraAccel_grpc_lib)

    add_executable(KvadraAccel_grpc_node_b src/apps/grpc_node_b_main.cpp)
    target_link_libraries(KvadraAccel_grpc_node_b PRIVATE KvadraAccel_grpc_lib)

    add_executable(KvadraAccel_grpc_callback_server src/apps/grpc_callback_server_main.cpp)
    target_link_libraries(KvadraAccel_grpc_callback_server PRIVATE KvadraAccel_grpc_lib)

    add_executable(KvadraAccel_grpc_callback_node_a src/apps/grpc_callback_node_a_main.cpp)
    target_link_libraries(KvadraAccel_grpc_callback_node_a PRIVATE KvadraAccel_grpc_lib)

    add_executable(KvadraAccel_grpc_callback_node_b src/apps/grpc_callback_node_b_main.cpp)
    target_link_libraries(KvadraAccel_grpc_callback_node_b PRIVATE KvadraAccel_grpc_lib)
endif()

option(KVADRA_ACCEL_BUILD_TESTS "Build unit tests" ON)

if(KVADRA_ACCEL_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(
        KvadraAccel_tests
        tests/blocking_queue_test.cpp
        tests/cli_test.cpp
        tests/config_test.cpp
        tests/domain_test.cpp
        tests/json_protocol_test.cpp
    )
    target_link_libraries(KvadraAccel_tests PRIVATE KvadraAccel_accel_lib Catch2::Catch2WithMain)

    if(KVADRA_ACCEL_BUILD_GRPC)
        target_sources(KvadraAccel_tests PRIVATE tests/proto_conversion_test.cpp)
        target_link_libraries(KvadraAccel_tests PRIVATE KvadraAccel_grpc_lib)
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(KvadraAccel_tests)

    add_test(
        NAME e2e_level1_pipeline
        COMMAND
            bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_level1_test.sh
            $<TARGET_FILE:KvadraAccel_server>
            $<TARGET_FILE:KvadraAccel_node_a>
            $<TARGET_FILE:KvadraAccel_node_b>
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(KVADRA_ACCEL_BUILD_GRPC)
        add_test(
            NAME integration_grpc_security
            COMMAND
                bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_grpc_security_test.sh
                $<TARGET_FILE:KvadraAccel_grpc_server>
                $<TARGET_FILE:KvadraAccel_grpc_node_a>
                $<TARGET_FILE:KvadraAccel_grpc_node_b>
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()
