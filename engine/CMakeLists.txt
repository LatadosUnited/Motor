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
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)  # os objetos vão para uma DLL

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

# ---------------------------------------------------------------------------
# Biblioteca compartilhada (libMotor.dll) com os objetos de todas as camadas: o editor e a DLL de scripts de
# cada projeto ligam nela, e assim compartilham o registro de componentes, o Input, o log e o ImGui.
# ---------------------------------------------------------------------------
add_library(Motor SHARED)

# Ligação direta: os objetos das camadas (e os do ImGui, biblioteca de objetos) entram na DLL. PRIVATE para
# o GLFW e o nlohmann_json não irem também para quem liga no Motor (haveria duas cópias no processo).
foreach(layer IN LISTS MOTOR_LAYERS)
    target_link_libraries(Motor PRIVATE motor_${layer})
endforeach()
target_link_libraries(Motor PRIVATE imgui glfw nlohmann_json::nlohmann_json)

# O que quem usa o Motor precisa: headers da engine e do ImGui, GLM.
target_include_directories(Motor PUBLIC include ${MOTOR_IMGUI_INCLUDE_DIRS})
target_link_libraries(Motor PUBLIC glm::glm)
target_compile_definitions(Motor PUBLIC GLM_FORCE_RADIANS)

# Sem __declspec(dllexport) no código: exporta todos os símbolos (engine, ImGui e GLFW).
if(MINGW)
    target_link_options(Motor PRIVATE -Wl,--export-all-symbols)
endif()

# ---------------------------------------------------------------------------
# SDK para os scripts dos projetos: o editor gera um CMakeLists por projeto que inclui MotorSDK.cmake (alvo
# importado Motor::Motor) e lê MotorSDK.json para saber qual toolchain usar (o mesmo deste build).
# ---------------------------------------------------------------------------
set(MOTOR_SDK_DIR ${CMAKE_BINARY_DIR}/bin/sdk)
set(MOTOR_SDK_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${glm_SOURCE_DIR} ${MOTOR_IMGUI_INCLUDE_DIRS})
file(GENERATE OUTPUT ${MOTOR_SDK_DIR}/MotorSDK.cmake CONTENT
"# Gerado pelo build do Motor. Não edite.
if(NOT TARGET Motor::Motor)
    add_library(Motor::Motor SHARED IMPORTED)
    set_target_properties(Motor::Motor PROPERTIES
        IMPORTED_LOCATION \"$<TARGET_FILE:Motor>\"
        IMPORTED_IMPLIB \"$<TARGET_LINKER_FILE:Motor>\"
        INTERFACE_INCLUDE_DIRECTORIES \"${MOTOR_SDK_INCLUDE_DIRS}\"
        INTERFACE_COMPILE_DEFINITIONS \"GLM_FORCE_RADIANS\"
        INTERFACE_COMPILE_FEATURES \"cxx_std_20\")
endif()
")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/MotorSDK.json.in ${MOTOR_SDK_DIR}/MotorSDK.json @ONLY)
