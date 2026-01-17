/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "MCPHandler.h"
#include <stdexcept>

namespace openscad::mcp {

MCPHandler::MCPHandler() {
  serverInfo_.name = "openscad-mcp";
  serverInfo_.version = "0.1.0";
  capabilities_.tools = true;
}

void MCPHandler::registerTool(std::unique_ptr<MCPTool> tool) {
  if (tool) {
    tools_[tool->name()] = std::move(tool);
  }
}

json MCPHandler::handleMessage(const json& message) {
  // Check for parse error marker
  if (message.contains("__parse_error__")) {
    return JsonRpcResponse::makeError(
        nullptr,
        ErrorCode::PARSE_ERROR,
        "Parse error: " + message.value("message", "Unknown parse error")
    ).toJson();
  }

  // Parse the request
  auto request = JsonRpcRequest::fromJson(message);
  if (!request.has_value()) {
    return JsonRpcResponse::makeError(
        nullptr,
        ErrorCode::INVALID_REQUEST,
        "Invalid JSON-RPC request"
    ).toJson();
  }

  return handleRequest(request.value()).toJson();
}

JsonRpcResponse MCPHandler::handleRequest(const JsonRpcRequest& request) {
  const auto& method = request.method;

  // Route to appropriate handler
  if (method == "initialize") {
    return handleInitialize(request);
  } else if (method == "notifications/initialized") {
    return handleInitialized(request);
  } else if (method == "tools/list") {
    return handleToolsList(request);
  } else if (method == "tools/call") {
    return handleToolsCall(request);
  } else if (method == "ping") {
    return handlePing(request);
  }

  // Unknown method
  return JsonRpcResponse::makeError(
      request.id,
      ErrorCode::METHOD_NOT_FOUND,
      "Method not found: " + method
  );
}

JsonRpcResponse MCPHandler::handleInitialize(const JsonRpcRequest& request) {
  // Build the result
  json result = {
    {"protocolVersion", MCP_PROTOCOL_VERSION},
    {"capabilities", capabilities_.toJson()},
    {"serverInfo", serverInfo_.toJson()}
  };

  initialized_ = true;
  return JsonRpcResponse::makeResult(request.id, result);
}

JsonRpcResponse MCPHandler::handleInitialized(const JsonRpcRequest& request) {
  // This is a notification, no response needed
  // But we return an empty result for consistency
  return JsonRpcResponse::makeResult(request.id, json::object());
}

JsonRpcResponse MCPHandler::handleToolsList(const JsonRpcRequest& request) {
  json toolsArray = json::array();

  for (const auto& [name, tool] : tools_) {
    toolsArray.push_back(tool->getDefinition().toJson());
  }

  json result = {{"tools", toolsArray}};
  return JsonRpcResponse::makeResult(request.id, result);
}

JsonRpcResponse MCPHandler::handleToolsCall(const JsonRpcRequest& request) {
  // Extract tool name and arguments
  if (!request.params.contains("name") || !request.params["name"].is_string()) {
    return JsonRpcResponse::makeError(
        request.id,
        ErrorCode::INVALID_PARAMS,
        "Missing or invalid 'name' parameter"
    );
  }

  std::string toolName = request.params["name"];
  json arguments = request.params.value("arguments", json::object());

  // Find the tool
  auto it = tools_.find(toolName);
  if (it == tools_.end()) {
    return JsonRpcResponse::makeError(
        request.id,
        ErrorCode::METHOD_NOT_FOUND,
        "Tool not found: " + toolName
    );
  }

  // Execute the tool
  try {
    MCPToolResult toolResult = it->second->execute(arguments);
    return JsonRpcResponse::makeResult(request.id, toolResult.toJson());
  } catch (const std::exception& e) {
    return JsonRpcResponse::makeError(
        request.id,
        ErrorCode::INTERNAL_ERROR,
        std::string("Tool execution error: ") + e.what()
    );
  }
}

JsonRpcResponse MCPHandler::handlePing(const JsonRpcRequest& request) {
  return JsonRpcResponse::makeResult(request.id, json::object());
}

}  // namespace openscad::mcp
