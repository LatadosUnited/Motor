# A engine é dividida em camadas. Cada camada é uma biblioteca de objetos com a sua pasta (src/<camada>/ e
# include/motor/<camada>/) e o seu CMakeLists.txt; a biblioteca Motor junta todas.
#
#   core -> renderer -> project -> assets -> scene -> app
#
# Uma camada só pode incluir headers dela mesma e das camadas à esquerda. O teste `test_layers` confere isso
# nos #include; a lista abaixo (MOTOR_LAYERS) é a fonte da ordem para ele.
set(MOTOR_LAYERS core renderer project assets scene app)
set(MOTOR_LAYERS ${MOTOR_LAYERS} PARENT_SCOPE)

# motor_add_layer(<camada> SOURCES <arquivos...> DEPENDS <camadas...> [PRIVATE_LIBS <alvos...>])
# Cria o alvo motor_<camada> com as configurações comuns da engine.
function(motor_add_layer layer)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;PRIVATE_LIBS" ${ARGN})
    set(target motor_${layer})
    add_library(${target} OBJECT ${ARG_SOURCES})

    target_include_directories(${target}
        PUBLIC  ${MOTOR_ENGINE_DIR}/include
        PRIVATE ${MOTOR_ENGINE_DIR}/src)

    foreach(dep IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} PUBLIC motor_${dep})
    endforeach()
    target_link_libraries(${target} PUBLIC glm::glm PRIVATE ${ARG_PRIVATE_LIBS})

    target_compile_definitions(${target}
        PUBLIC  GLM_FORCE_RADIANS
        PRIVATE MOTOR_ENGINE_ASSETS="${MOTOR_ENGINE_DIR}/assets"
                GLFW_INCLUDE_NONE
                NOMINMAX
                WIN32_LEAN_AND_MEAN)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

set(MOTOR_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
foreach(layer IN LISTS MOTOR_LAYERS)
    add_subdirectory(src/${layer})
endforeach()

# Biblioteca única usada pelo editor, sandbox, scripts e testes: os objetos de todas as camadas.
add_library(Motor STATIC)
foreach(layer IN LISTS MOTOR_LAYERS)
    target_link_libraries(Motor PUBLIC motor_${layer})
endforeach()
