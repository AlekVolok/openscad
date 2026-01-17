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

#include <memory>
#include <istream>
#include <ostream>
#include "MCPTypes.h"
#include "MCPTransport.h"
#include "MCPHandler.h"

namespace openscad::mcp {

/**
 * MCPServer is the main entry point for the MCP server.
 * It manages the transport layer and request handling.
 *
 * Usage:
 *   MCPServer server;
 *   server.registerTool(std::make_unique<MyTool>());
 *   server.run();  // Blocks until input is closed
 */
class MCPServer {
public:
  /**
   * Create an MCP server using stdin/stdout for communication.
   */
  MCPServer();

  /**
   * Create an MCP server using custom input/output streams.
   * This is useful for testing.
   */
  MCPServer(std::istream& input, std::ostream& output);

  ~MCPServer() = default;

  // Non-copyable
  MCPServer(const MCPServer&) = delete;
  MCPServer& operator=(const MCPServer&) = delete;

  /**
   * Register a tool with the server.
   * Must be called before run().
   */
  void registerTool(std::unique_ptr<MCPTool> tool);

  /**
   * Run the server's main loop.
   * Blocks until the input stream is closed or an error occurs.
   * Returns 0 on clean shutdown, non-zero on error.
   */
  int run();

  /**
   * Stop the server (can be called from another thread).
   */
  void stop();

  /**
   * Check if the server is running.
   */
  bool isRunning() const { return running_; }

  /**
   * Get the handler for direct access (useful for testing).
   */
  MCPHandler& handler() { return handler_; }

private:
  std::unique_ptr<MCPTransport> transport_;
  MCPHandler handler_;
  bool running_ = false;
  bool shouldStop_ = false;
};

/**
 * Entry point for running the MCP server from the command line.
 * This function is called from openscad.cc when --mcp-server is specified.
 */
int runMCPServer();

}  // namespace openscad::mcp
