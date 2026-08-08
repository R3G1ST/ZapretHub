cmake_minimum_required(VERSION 3.10)
project(Veto VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_RELEASE "/O2 /W3")
set(CMAKE_C_FLAGS_DEBUG "/Zi /Od /W3")

include_directories(${CMAKE_SOURCE_DIR}/include)

set(VETO_SOURCES
    src/core/packet.c
    src/core/tcp_reassembly.c
    src/core/veto_engine.c
    src/protocols/proto_detect.c
    src/attacks/attacks.c
    src/config/config.c
    src/main.c
)

add_executable(veto ${VETO_SOURCES})

target_link_libraries(veto
    ws2_32
    advapi32
    shlwapi
)

if(EXISTS "${CMAKE_SOURCE_DIR}/lib/windivert.h")
    include_directories(${CMAKE_SOURCE_DIR}/lib)
    target_link_libraries(veto ${CMAKE_SOURCE_DIR}/lib/windivert.lib)
endif()

install(TARGETS veto RUNTIME DESTINATION bin)

add_custom_command(TARGET veto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/veto.conf"
    "$<TARGET_FILE_DIR:veto>/veto.conf"
)

message(STATUS "Veto ${PROJECT_VERSION} configured")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
