add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Motor)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Sandbox PRIVATE -Wall -Wextra)
endif()
