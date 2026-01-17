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
#include <optional>
#include <istream>
#include <ostream>
#include "MCPTypes.h"

namespace openscad::mcp {

/**
 * MCPTransport handles the low-level reading and writing of JSON-RPC messages.
 *
 * The MCP protocol uses newline-delimited JSON (NDJSON) format where each
 * message is a complete JSON object on a single line.
 */
class MCPTransport {
public:
  MCPTransport(std::istream& input, std::ostream& output);
  ~MCPTransport() = default;

  // Non-copyable
  MCPTransport(const MCPTransport&) = delete;
  MCPTransport& operator=(const MCPTransport&) = delete;

  /**
   * Read a single JSON-RPC request from the input stream.
   * Returns std::nullopt on EOF or read error.
   */
  std::optional<json> readMessage();

  /**
   * Write a JSON-RPC response to the output stream.
   */
  void writeMessage(const json& message);

  /**
   * Check if the transport is still valid (no errors, not at EOF).
   */
  bool isValid() const;

private:
  std::istream& input_;
  std::ostream& output_;
  bool valid_ = true;
};

}  // namespace openscad::mcp
