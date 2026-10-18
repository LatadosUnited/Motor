# Monta a pasta de instalação autossuficiente de uma versão do Motor (Fase 14), que o Hub registra em
# %LOCALAPPDATA%\Motor\Versões\<versão>\. Rodado pelo alvo motor_package (CMakeLists.txt da raiz) com cmake -P:
#
#   Motor-<versão>/
#     MotorEditor.exe, Player.exe     o editor e o executável do jogo exportado (Arquivo > Exportar jogo)
#     libMotor.dll + runtime do GCC   as mesmas DLLs que a exportação copia (kRuntimeDlls, editor/src/GameExport.h)
#     engine/shaders, engine/fonts    assets da engine; achados ao lado do .exe (motor/core/EngineAssets.h)
#     templates/Scripts/              modelos de script (projetos novos, menu Scripts)
#     docs/                           documentação da versão (scripts.md, scripts-api.md, ...)
#     sdk/                            para o editor compilar os scripts dos projetos sem o código-fonte:
#       MotorSDK.json, MotorSDK.cmake   toolchain (o mesmo do build: MSYS2 UCRT64) e alvo importado Motor::Motor
#       include/, lib/libMotor.dll.a    headers da engine, GLM e ImGui; biblioteca de importação
#     motor-version.json              {version, scriptApiVersion}: o que o Hub lê para saber qual versão é
#
# A pasta de saída é apagada antes, mas só se for uma montagem anterior (tem motor-version.json) ou estiver vazia.
#
# Variáveis (-D): OUT BIN SRC VERSION BUILD_TYPE IMPLIB IMGUI_DIR GLM_DIR GEN_DIR
#                 CMAKE_EXE GENERATOR MAKE_PROGRAM C_COMPILER CXX_COMPILER

cmake_minimum_required(VERSION 3.24)

foreach(var OUT BIN SRC VERSION IMPLIB IMGUI_DIR GLM_DIR GEN_DIR CMAKE_EXE GENERATOR CXX_COMPILER)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "MotorPackage.cmake: falta -D${var}")
    endif()
endforeach()

# Mesma lista de GameExport::kRuntimeDlls (editor/src/GameExport.h): lida de lá para não ficarem diferentes.
file(READ "${SRC}/editor/src/GameExport.h" export_header)
string(REGEX MATCH "kRuntimeDlls\\[\\] = {([^}]*)}" _ "${export_header}")
string(REGEX MATCHALL "\"[^\"]+\"" runtime_dlls "${CMAKE_MATCH_1}")
string(REPLACE "\"" "" runtime_dlls "${runtime_dlls}")
if(NOT runtime_dlls)
    message(FATAL_ERROR "kRuntimeDlls não encontrada em editor/src/GameExport.h")
endif()

# Versão da interface com a DLL de scripts (kScriptApiVersion em motor/scene/ScriptModule.h).
file(READ "${SRC}/engine/include/motor/scene/ScriptModule.h" module_header)
string(REGEX MATCH "kScriptApiVersion = ([0-9]+)" _ "${module_header}")
set(script_api "${CMAKE_MATCH_1}")
if(script_api STREQUAL "")
    message(FATAL_ERROR "kScriptApiVersion não encontrada em ScriptModule.h")
endif()

# --- Pasta de saída ---
if(EXISTS "${OUT}")
    file(GLOB existing "${OUT}/*")
    if(existing AND NOT EXISTS "${OUT}/motor-version.json")
        message(FATAL_ERROR "A pasta '${OUT}' não está vazia e não é uma instalação do Motor: escolha outra.")
    endif()
    file(REMOVE_RECURSE "${OUT}")
endif()
file(MAKE_DIRECTORY "${OUT}")

# --- Executáveis e DLLs ---
foreach(file MotorEditor.exe Player.exe ${runtime_dlls})
    if(NOT EXISTS "${BIN}/${file}")
        message(FATAL_ERROR "'${file}' não encontrado em '${BIN}': compile o Motor antes.")
    endif()
    file(COPY "${BIN}/${file}" DESTINATION "${OUT}")
endforeach()

# --- Assets da engine, modelos de script e documentação ---
file(COPY "${SRC}/engine/assets/shaders" "${SRC}/engine/assets/fonts" DESTINATION "${OUT}/engine")
file(COPY "${SRC}/editor/templates/" DESTINATION "${OUT}/templates")
file(COPY "${SRC}/docs/" DESTINATION "${OUT}/docs")

# --- SDK dos scripts ---
set(sdk "${OUT}/sdk")
file(COPY "${SRC}/engine/include/motor" DESTINATION "${sdk}/include" FILES_MATCHING PATTERN "*.h" PATTERN "*.inl")
file(COPY "${GEN_DIR}/motor" DESTINATION "${sdk}/include")
file(COPY "${GLM_DIR}/glm" DESTINATION "${sdk}/include" FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
foreach(dir "${IMGUI_DIR}" "${IMGUI_DIR}/backends" "${IMGUI_DIR}/misc/cpp")
    file(GLOB headers "${dir}/*.h")
    file(COPY ${headers} DESTINATION "${sdk}/include")
endforeach()
file(COPY "${IMPLIB}" DESTINATION "${sdk}/lib")
get_filename_component(implib_name "${IMPLIB}" NAME)
if(EXISTS "${BIN}/sdk/MotorScriptPch.h")
    file(COPY "${BIN}/sdk/MotorScriptPch.h" DESTINATION "${sdk}")
endif()
if(EXISTS "${BIN}/sdk/MotorScriptPch.h.gch")
    file(COPY "${BIN}/sdk/MotorScriptPch.h.gch" DESTINATION "${sdk}")
endif()

# Caminhos relativos à pasta do SDK: a instalação pode ser movida (o Hub copia para Versões\<versão>).
file(WRITE "${sdk}/MotorSDK.cmake"
"# Gerado pelo alvo motor_package (pasta de instalação do Motor ${VERSION}). Não edite.
set(MOTOR_SDK_PCH \"\${CMAKE_CURRENT_LIST_DIR}/MotorScriptPch.h\")
if(NOT TARGET Motor::Motor)
    add_library(Motor::Motor SHARED IMPORTED)
    set_target_properties(Motor::Motor PROPERTIES
        IMPORTED_LOCATION \"\${CMAKE_CURRENT_LIST_DIR}/../libMotor.dll\"
        IMPORTED_IMPLIB \"\${CMAKE_CURRENT_LIST_DIR}/lib/${implib_name}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/include\"
        INTERFACE_COMPILE_DEFINITIONS \"GLM_FORCE_RADIANS\"
        INTERFACE_COMPILE_FEATURES \"cxx_std_20\")
endif()
")

# O toolchain continua o do MSYS2 que compilou esta versão (o editor chama o cmake e o g++ de lá).
file(WRITE "${sdk}/MotorSDK.json"
"{
  \"format\": \"motor-sdk\",
  \"version\": 1,
  \"cmake\": \"${CMAKE_EXE}\",
  \"generator\": \"${GENERATOR}\",
  \"makeProgram\": \"${MAKE_PROGRAM}\",
  \"cCompiler\": \"${C_COMPILER}\",
  \"cxxCompiler\": \"${CXX_COMPILER}\",
  \"buildType\": \"${BUILD_TYPE}\",
  \"sdkCMake\": \"MotorSDK.cmake\"
}
")

# --- Marca da versão (lida pelo Hub) ---
string(TIMESTAMP built "%Y-%m-%dT%H:%M:%S")
file(WRITE "${OUT}/motor-version.json"
"{
  \"format\": \"motor-version\",
  \"formatVersion\": 1,
  \"version\": \"${VERSION}\",
  \"scriptApiVersion\": ${script_api},
  \"buildType\": \"${BUILD_TYPE}\",
  \"built\": \"${built}\",
  \"editor\": \"MotorEditor.exe\"
}
")

message(STATUS "Motor ${VERSION} montado em ${OUT}")
