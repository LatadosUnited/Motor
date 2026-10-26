#pragma once

// Lista de ferramentas do MCP do editor sem janela (MotorEditor --mcp-ferramentas <arquivo>, Hub residente). O alvo
// motor_package grava a lista na pasta da versão (mcp-ferramentas.json) e o Hub a usa para responder o initialize e o
// tools/list de /mcpengine/<Projeto> com o editor fechado.
//
//   {"format": "motor-mcp-tools", "version": 1, "engineVersion": "0.3.0", "scriptApiVersion": 12,
//    "initialize": {...resultado do initialize...}, "tools": [...o tools/list...]}

#include "Mcp.h"

#include <filesystem>
#include <string>

namespace editor::mcp {

inline constexpr const char* kToolListFormat = "motor-mcp-tools";
inline constexpr int kToolListVersion = 1;

// Monta as peças do editor sem janela nem OpenGL (como o EditorApp, mas sem Init) e lê as ferramentas do mesmo
// McpController que o editor aberto usa: a lista não fica diferente da de verdade.
Json EditorToolList();

bool WriteEditorToolList(const std::filesystem::path& file, std::string& error);

} // namespace editor::mcp
