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

#include "MCPTransport.h"
#include <iostream>

namespace openscad::mcp {

MCPTransport::MCPTransport(std::istream& input, std::ostream& output)
    : input_(input), output_(output) {}

std::optional<json> MCPTransport::readMessage() {
  if (!valid_) {
    return std::nullopt;
  }

  std::string line;
  if (!std::getline(input_, line)) {
    valid_ = false;
    return std::nullopt;
  }

  // Skip empty lines
  if (line.empty()) {
    return readMessage();
  }

  try {
    return json::parse(line);
  } catch (const json::parse_error& e) {
    // Return a parse error that can be handled by the server
    // We return an object with a special marker so the server knows it's a parse error
    return json{{"__parse_error__", true}, {"message", e.what()}};
  }
}

void MCPTransport::writeMessage(const json& message) {
  if (!valid_) {
    return;
  }

  output_ << message.dump() << "\n";
  output_.flush();
}

bool MCPTransport::isValid() const {
  return valid_ && !input_.eof() && !input_.fail();
}

}  // namespace openscad::mcp
