# Recurso VERSIONINFO dos executáveis da distribuição (Fase 15): a versão aparece nas Propriedades do arquivo e o
# MotorServidor lê a do MotorHub.exe ao publicar (--publicar-hub).
#
#   motor_version_resource(<alvo> <versão x.y.z> "<descrição>" <arquivo.exe> [recursos .rc a mais])
#
# Textos só em ASCII: o windres lê o .rc na página de código do sistema.
function(motor_version_resource target version description filename)
    if(NOT WIN32)
        return()
    endif()
    string(REPLACE "." ";" parts "${version}")
    list(GET parts 0 MOTOR_RC_MAJOR)
    list(GET parts 1 MOTOR_RC_MINOR)
    list(GET parts 2 MOTOR_RC_PATCH)
    set(MOTOR_RC_VERSION "${version}")
    set(MOTOR_RC_DESCRIPTION "${description}")
    set(MOTOR_RC_FILENAME "${filename}")
    set(MOTOR_RC_EXTRA "")
    foreach(extra IN LISTS ARGN)
        string(APPEND MOTOR_RC_EXTRA "${extra}\n")
    endforeach()
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/VersionInfo.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
endfunction()
