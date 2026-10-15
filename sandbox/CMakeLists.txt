add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Motor)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Sandbox PRIVATE -Wall -Wextra)
endif()

# Com MinGW, linka o runtime estaticamente para o .exe rodar fora do shell do MSYS2.
if(MINGW)
    target_link_options(Sandbox PRIVATE -static)
endif()
