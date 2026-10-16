# Projeto: pasta + arquivo .mproject.
motor_add_layer(project
    SOURCES
        Project.cpp
    DEPENDS core
    PRIVATE_LIBS nlohmann_json::nlohmann_json)
