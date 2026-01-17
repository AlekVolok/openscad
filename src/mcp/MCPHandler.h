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

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "MCPTypes.h"

namespace openscad::mcp {

/**
 * MCPTool is the abstract base class for all MCP tools.
 * Each tool provides a specific capability that can be invoked by the client.
 */
class MCPTool {
public:
  virtual ~MCPTool() = default;

  /// Returns the unique name of this tool
  virtual std::string name() const = 0;

  /// Returns a human-readable description of what this tool does
  virtual std::string description() const = 0;

  /// Returns the JSON Schema describing the tool's input parameters
  virtual json inputSchema() const = 0;

  /// Execute the tool with the given parameters and return the result
  virtual MCPToolResult execute(const json& params) = 0;

  /// Get the tool definition for listing
  MCPToolDefinition getDefinition() const {
    return {name(), description(), inputSchema()};
  }
};

/**
 * MCPHandler routes incoming JSON-RPC requests to the appropriate handlers.
 * It manages the MCP protocol lifecycle and tool registry.
 */
class MCPHandler {
public:
  MCPHandler();
  ~MCPHandler() = default;

  // Non-copyable
  MCPHandler(const MCPHandler&) = delete;
  MCPHandler& operator=(const MCPHandler&) = delete;

  /**
   * Register a tool with the handler.
   * Takes ownership of the tool.
   */
  void registerTool(std::unique_ptr<MCPTool> tool);

  /**
   * Handle an incoming JSON-RPC request and return a response.
   */
  JsonRpcResponse handleRequest(const JsonRpcRequest& request);

  /**
   * Handle a raw JSON message (parses and dispatches).
   * Returns the response as JSON.
   */
  json handleMessage(const json& message);

  /**
   * Check if the handler has been initialized.
   */
  bool isInitialized() const { return initialized_; }

private:
  // MCP method handlers
  JsonRpcResponse handleInitialize(const JsonRpcRequest& request);
  JsonRpcResponse handleInitialized(const JsonRpcRequest& request);
  JsonRpcResponse handleToolsList(const JsonRpcRequest& request);
  JsonRpcResponse handleToolsCall(const JsonRpcRequest& request);
  JsonRpcResponse handlePing(const JsonRpcRequest& request);

  // Tool registry
  std::unordered_map<std::string, std::unique_ptr<MCPTool>> tools_;

  // Server state
  bool initialized_ = false;
  MCPServerInfo serverInfo_;
  MCPServerCapabilities capabilities_;
};

}  // namespace openscad::mcp
