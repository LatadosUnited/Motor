# Código comum à engine e aos programas que não ligam nela (hub, distribuição, servidor, instalador). Só a
# biblioteca padrão e o Windows: nada da engine, para o hub continuar independente de qualquer versão do Motor.
#
# motor_common: gravação segura de arquivos (motor/common/FileWrite.h), testada em tests/test_file_write.cpp.
add_library(motor_common STATIC src/FileWrite.cpp)
target_include_directories(motor_common PUBLIC include)
set_target_properties(motor_common PROPERTIES POSITION_INDEPENDENT_CODE ON)  # também vai dentro da libMotor.dll
target_compile_definitions(motor_common PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(motor_common PRIVATE -Wall -Wextra -Wpedantic)
endif()
