#pragma once

// Protocolo e servidor MCP vêm da biblioteca motor_mcp (mcp/, sem a engine, também usada pelo Hub); o editor os
// usa com os nomes de sempre em editor::mcp.
#include <motor/mcp/McpProtocol.h>
#include <motor/mcp/McpServer.h>

namespace editor::mcp {

using motor::mcp::Deferred;
using motor::mcp::Json;
using motor::mcp::Prop;
using motor::mcp::Protocol;
using motor::mcp::Schema;
using motor::mcp::Server;
using motor::mcp::Tool;
using motor::mcp::ToolError;
using motor::mcp::ToolResult;

} // namespace editor::mcp
