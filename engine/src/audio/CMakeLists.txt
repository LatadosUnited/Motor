# Áudio (Fase 8): sistema do SceneRuntime que toca os componentes AudioSource com o miniaudio, ouvidos pelo
# AudioListener (ou pela câmera). O miniaudio não aparece nos headers públicos: os scripts não precisam dele.
motor_add_layer(audio
    SOURCES
        AudioSystem.cpp
        MiniaudioImpl.c
    DEPENDS scene)

# Headers do miniaudio como "de sistema" (os avisos de lá não são do nosso código) e a implementação dele
# compilada sem avisos.
target_include_directories(motor_audio SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "-w")
