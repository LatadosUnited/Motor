# MCP (Model Context Protocol): protocolo JSON-RPC (motor/mcp/McpProtocol.h) e transporte HTTP com cpp-httplib
# (motor/mcp/McpServer.h), usados pelo servidor do editor (Fase 13) e pelo do Hub (Hub residente). Como o hub, não
# liga na libMotor.dll: só nlohmann_json, o header da cpp-httplib e o Winsock. Testado em tests/test_mcp.cpp.
add_library(motor_mcp STATIC
    src/McpProtocol.cpp
    src/McpServer.cpp)
target_include_directories(motor_mcp PUBLIC include PRIVATE ${httplib_SOURCE_DIR})
target_link_libraries(motor_mcp PUBLIC nlohmann_json::nlohmann_json)
if(WIN32)
    # httplib: Winsock e Windows 10+.
    target_compile_definitions(motor_mcp PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(motor_mcp PUBLIC ws2_32)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(motor_mcp PRIVATE -Wall -Wextra -Wpedantic)
endif()
