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
#include <optional>
#include <variant>
#include "ext/json/json.hpp"

namespace openscad::mcp {

using json = nlohmann::json;

// MCP Protocol version
constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

// JSON-RPC error codes
namespace ErrorCode {
  constexpr int PARSE_ERROR = -32700;
  constexpr int INVALID_REQUEST = -32600;
  constexpr int METHOD_NOT_FOUND = -32601;
  constexpr int INVALID_PARAMS = -32602;
  constexpr int INTERNAL_ERROR = -32603;
}

// MCP Tool definition
struct MCPToolDefinition {
  std::string name;
  std::string description;
  json inputSchema;  // JSON Schema for tool parameters

  json toJson() const {
    return {
      {"name", name},
      {"description", description},
      {"inputSchema", inputSchema}
    };
  }
};

// MCP Tool result content types
struct TextContent {
  std::string text;
  std::string type = "text";

  json toJson() const {
    return {{"type", type}, {"text", text}};
  }
};

struct ImageContent {
  std::string data;      // Base64 encoded
  std::string mimeType;  // e.g., "image/png"
  std::string type = "image";

  json toJson() const {
    return {{"type", type}, {"data", data}, {"mimeType", mimeType}};
  }
};

using ContentItem = std::variant<TextContent, ImageContent>;

// MCP Tool call result
struct MCPToolResult {
  std::vector<ContentItem> content;
  bool isError = false;

  json toJson() const {
    json contentArray = json::array();
    for (const auto& item : content) {
      std::visit([&contentArray](const auto& c) {
        contentArray.push_back(c.toJson());
      }, item);
    }
    return {{"content", contentArray}, {"isError", isError}};
  }
};

// Server capabilities
struct MCPServerCapabilities {
  bool tools = true;

  json toJson() const {
    json caps = json::object();
    if (tools) {
      caps["tools"] = json::object();
    }
    return caps;
  }
};

// Server info
struct MCPServerInfo {
  std::string name = "openscad-mcp";
  std::string version = "0.1.0";

  json toJson() const {
    return {{"name", name}, {"version", version}};
  }
};

// JSON-RPC Request
struct JsonRpcRequest {
  std::string jsonrpc = "2.0";
  std::variant<int, std::string, std::nullptr_t> id = nullptr;
  std::string method;
  json params = json::object();

  static std::optional<JsonRpcRequest> fromJson(const json& j) {
    JsonRpcRequest req;

    if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
      return std::nullopt;
    }
    req.jsonrpc = j["jsonrpc"];

    if (j.contains("id")) {
      if (j["id"].is_number_integer()) {
        req.id = j["id"].get<int>();
      } else if (j["id"].is_string()) {
        req.id = j["id"].get<std::string>();
      }
    }

    if (!j.contains("method") || !j["method"].is_string()) {
      return std::nullopt;
    }
    req.method = j["method"];

    if (j.contains("params")) {
      req.params = j["params"];
    }

    return req;
  }
};

// JSON-RPC Response
struct JsonRpcResponse {
  std::string jsonrpc = "2.0";
  std::variant<int, std::string, std::nullptr_t> id = nullptr;
  std::optional<json> result;
  std::optional<json> error;

  json toJson() const {
    json j;
    j["jsonrpc"] = jsonrpc;

    std::visit([&j](const auto& idVal) {
      using T = std::decay_t<decltype(idVal)>;
      if constexpr (std::is_same_v<T, std::nullptr_t>) {
        j["id"] = nullptr;
      } else {
        j["id"] = idVal;
      }
    }, id);

    if (result.has_value()) {
      j["result"] = result.value();
    } else if (error.has_value()) {
      j["error"] = error.value();
    }

    return j;
  }

  static JsonRpcResponse makeResult(
      const std::variant<int, std::string, std::nullptr_t>& id,
      const json& result) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = result;
    return resp;
  }

  static JsonRpcResponse makeError(
      const std::variant<int, std::string, std::nullptr_t>& id,
      int code,
      const std::string& message,
      const std::optional<json>& data = std::nullopt) {
    JsonRpcResponse resp;
    resp.id = id;
    json err = {{"code", code}, {"message", message}};
    if (data.has_value()) {
      err["data"] = data.value();
    }
    resp.error = err;
    return resp;
  }
};

}  // namespace openscad::mcp
