# Aplicação: junta janela, renderer e ImGui no loop principal (motor::Application).
motor_add_layer(app
    SOURCES
        Application.cpp
    DEPENDS core renderer)
