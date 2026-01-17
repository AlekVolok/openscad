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

#include "MCPServer.h"
#include <iostream>
#include <csignal>

namespace openscad::mcp {

// Global flag for signal handling
static volatile std::sig_atomic_t g_shouldStop = 0;

static void signalHandler(int /*signal*/) {
  g_shouldStop = 1;
}

MCPServer::MCPServer()
    : transport_(std::make_unique<MCPTransport>(std::cin, std::cout)) {
  // Set up signal handling for graceful shutdown
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
}

MCPServer::MCPServer(std::istream& input, std::ostream& output)
    : transport_(std::make_unique<MCPTransport>(input, output)) {}

void MCPServer::registerTool(std::unique_ptr<MCPTool> tool) {
  handler_.registerTool(std::move(tool));
}

int MCPServer::run() {
  running_ = true;
  shouldStop_ = false;

  while (!shouldStop_ && !g_shouldStop && transport_->isValid()) {
    auto message = transport_->readMessage();
    if (!message.has_value()) {
      // EOF or error
      break;
    }

    json response = handler_.handleMessage(message.value());
    transport_->writeMessage(response);
  }

  running_ = false;
  return 0;
}

void MCPServer::stop() {
  shouldStop_ = true;
}

int runMCPServer() {
  MCPServer server;
  return server.run();
}

}  // namespace openscad::mcp
