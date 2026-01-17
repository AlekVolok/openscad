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

#ifdef ENABLE_MCP

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include "mcp/MCPTypes.h"
#include "mcp/MCPTransport.h"
#include "mcp/MCPHandler.h"
#include "mcp/MCPServer.h"

using namespace openscad::mcp;

// ============================================================================
// MCPTypes Tests
// ============================================================================

TEST_CASE("JsonRpcRequest parsing", "[mcp][types]") {
  SECTION("Valid request with integer id") {
    json j = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "test/method"},
      {"params", {{"key", "value"}}}
    };

    auto req = JsonRpcRequest::fromJson(j);
    REQUIRE(req.has_value());
    REQUIRE(req->method == "test/method");
    REQUIRE(std::holds_alternative<int>(req->id));
    REQUIRE(std::get<int>(req->id) == 1);
    REQUIRE(req->params["key"] == "value");
  }

  SECTION("Valid request with string id") {
    json j = {
      {"jsonrpc", "2.0"},
      {"id", "request-1"},
      {"method", "test/method"}
    };

    auto req = JsonRpcRequest::fromJson(j);
    REQUIRE(req.has_value());
    REQUIRE(std::holds_alternative<std::string>(req->id));
    REQUIRE(std::get<std::string>(req->id) == "request-1");
  }

  SECTION("Invalid request - missing jsonrpc") {
    json j = {
      {"id", 1},
      {"method", "test/method"}
    };

    auto req = JsonRpcRequest::fromJson(j);
    REQUIRE_FALSE(req.has_value());
  }

  SECTION("Invalid request - wrong jsonrpc version") {
    json j = {
      {"jsonrpc", "1.0"},
      {"id", 1},
      {"method", "test/method"}
    };

    auto req = JsonRpcRequest::fromJson(j);
    REQUIRE_FALSE(req.has_value());
  }

  SECTION("Invalid request - missing method") {
    json j = {
      {"jsonrpc", "2.0"},
      {"id", 1}
    };

    auto req = JsonRpcRequest::fromJson(j);
    REQUIRE_FALSE(req.has_value());
  }
}

TEST_CASE("JsonRpcResponse creation", "[mcp][types]") {
  SECTION("Success response") {
    auto resp = JsonRpcResponse::makeResult(1, {{"status", "ok"}});
    json j = resp.toJson();

    REQUIRE(j["jsonrpc"] == "2.0");
    REQUIRE(j["id"] == 1);
    REQUIRE(j.contains("result"));
    REQUIRE(j["result"]["status"] == "ok");
    REQUIRE_FALSE(j.contains("error"));
  }

  SECTION("Error response") {
    auto resp = JsonRpcResponse::makeError(1, ErrorCode::METHOD_NOT_FOUND, "Method not found");
    json j = resp.toJson();

    REQUIRE(j["jsonrpc"] == "2.0");
    REQUIRE(j["id"] == 1);
    REQUIRE(j.contains("error"));
    REQUIRE(j["error"]["code"] == ErrorCode::METHOD_NOT_FOUND);
    REQUIRE(j["error"]["message"] == "Method not found");
    REQUIRE_FALSE(j.contains("result"));
  }
}

TEST_CASE("MCPToolResult serialization", "[mcp][types]") {
  SECTION("Text content") {
    MCPToolResult result;
    result.content.push_back(TextContent{"Hello, World!"});

    json j = result.toJson();
    REQUIRE(j["isError"] == false);
    REQUIRE(j["content"].size() == 1);
    REQUIRE(j["content"][0]["type"] == "text");
    REQUIRE(j["content"][0]["text"] == "Hello, World!");
  }

  SECTION("Image content") {
    MCPToolResult result;
    result.content.push_back(ImageContent{"base64data", "image/png"});

    json j = result.toJson();
    REQUIRE(j["content"][0]["type"] == "image");
    REQUIRE(j["content"][0]["data"] == "base64data");
    REQUIRE(j["content"][0]["mimeType"] == "image/png");
  }

  SECTION("Error result") {
    MCPToolResult result;
    result.isError = true;
    result.content.push_back(TextContent{"Something went wrong"});

    json j = result.toJson();
    REQUIRE(j["isError"] == true);
  }
}

// ============================================================================
// MCPTransport Tests
// ============================================================================

TEST_CASE("MCPTransport read/write", "[mcp][transport]") {
  SECTION("Read valid JSON") {
    std::istringstream input("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}\n");
    std::ostringstream output;

    MCPTransport transport(input, output);
    auto msg = transport.readMessage();

    REQUIRE(msg.has_value());
    REQUIRE((*msg)["method"] == "test");
  }

  SECTION("Read invalid JSON returns parse error marker") {
    std::istringstream input("not valid json\n");
    std::ostringstream output;

    MCPTransport transport(input, output);
    auto msg = transport.readMessage();

    REQUIRE(msg.has_value());
    REQUIRE((*msg).contains("__parse_error__"));
  }

  SECTION("Write JSON") {
    std::istringstream input;
    std::ostringstream output;

    MCPTransport transport(input, output);
    json msg = {{"test", "value"}};
    transport.writeMessage(msg);

    REQUIRE(output.str() == "{\"test\":\"value\"}\n");
  }

  SECTION("EOF returns nullopt") {
    std::istringstream input("");  // Empty input = immediate EOF
    std::ostringstream output;

    MCPTransport transport(input, output);
    auto msg = transport.readMessage();

    REQUIRE_FALSE(msg.has_value());
    REQUIRE_FALSE(transport.isValid());
  }
}

// ============================================================================
// MCPHandler Tests
// ============================================================================

// Simple test tool for testing
class EchoTool : public MCPTool {
public:
  std::string name() const override { return "echo"; }
  std::string description() const override { return "Echoes input back"; }
  json inputSchema() const override {
    return {
      {"type", "object"},
      {"properties", {
        {"message", {{"type", "string"}}}
      }},
      {"required", json::array({"message"})}
    };
  }
  MCPToolResult execute(const json& params) override {
    MCPToolResult result;
    result.content.push_back(TextContent{params.value("message", "")});
    return result;
  }
};

TEST_CASE("MCPHandler initialization", "[mcp][handler]") {
  MCPHandler handler;

  SECTION("Not initialized initially") {
    REQUIRE_FALSE(handler.isInitialized());
  }

  SECTION("Initialize request") {
    json initRequest = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "initialize"},
      {"params", {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"clientInfo", {{"name", "test-client"}, {"version", "1.0.0"}}}
      }}
    };

    json response = handler.handleMessage(initRequest);

    REQUIRE(response["jsonrpc"] == "2.0");
    REQUIRE(response["id"] == 1);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"]["protocolVersion"] == MCP_PROTOCOL_VERSION);
    REQUIRE(response["result"]["serverInfo"]["name"] == "openscad-mcp");
    REQUIRE(handler.isInitialized());
  }
}

TEST_CASE("MCPHandler tools", "[mcp][handler]") {
  MCPHandler handler;
  handler.registerTool(std::make_unique<EchoTool>());

  SECTION("List tools") {
    json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "tools/list"}
    };

    json response = handler.handleMessage(request);

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"]["tools"].is_array());
    REQUIRE(response["result"]["tools"].size() == 1);
    REQUIRE(response["result"]["tools"][0]["name"] == "echo");
  }

  SECTION("Call tool successfully") {
    json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "tools/call"},
      {"params", {
        {"name", "echo"},
        {"arguments", {{"message", "Hello!"}}}
      }}
    };

    json response = handler.handleMessage(request);

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"]["content"][0]["text"] == "Hello!");
  }

  SECTION("Call non-existent tool") {
    json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "tools/call"},
      {"params", {
        {"name", "nonexistent"}
      }}
    };

    json response = handler.handleMessage(request);

    REQUIRE(response.contains("error"));
    REQUIRE(response["error"]["code"] == ErrorCode::METHOD_NOT_FOUND);
  }
}

TEST_CASE("MCPHandler ping", "[mcp][handler]") {
  MCPHandler handler;

  json request = {
    {"jsonrpc", "2.0"},
    {"id", 1},
    {"method", "ping"}
  };

  json response = handler.handleMessage(request);

  REQUIRE(response.contains("result"));
  REQUIRE(response["result"].is_object());
}

TEST_CASE("MCPHandler unknown method", "[mcp][handler]") {
  MCPHandler handler;

  json request = {
    {"jsonrpc", "2.0"},
    {"id", 1},
    {"method", "unknown/method"}
  };

  json response = handler.handleMessage(request);

  REQUIRE(response.contains("error"));
  REQUIRE(response["error"]["code"] == ErrorCode::METHOD_NOT_FOUND);
}

// ============================================================================
// MCPServer Integration Tests
// ============================================================================

TEST_CASE("MCPServer full workflow", "[mcp][server]") {
  std::string input =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n"
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{\"message\":\"test\"}}}\n";

  std::istringstream inputStream(input);
  std::ostringstream outputStream;

  MCPServer server(inputStream, outputStream);
  server.registerTool(std::make_unique<EchoTool>());

  int result = server.run();
  REQUIRE(result == 0);

  // Parse output lines
  std::string output = outputStream.str();
  std::istringstream outputReader(output);
  std::string line;
  std::vector<json> responses;

  while (std::getline(outputReader, line)) {
    if (!line.empty()) {
      responses.push_back(json::parse(line));
    }
  }

  REQUIRE(responses.size() == 3);

  // Check initialize response
  REQUIRE(responses[0]["id"] == 1);
  REQUIRE(responses[0].contains("result"));
  REQUIRE(responses[0]["result"]["protocolVersion"] == MCP_PROTOCOL_VERSION);

  // Check tools/list response
  REQUIRE(responses[1]["id"] == 2);
  REQUIRE(responses[1]["result"]["tools"].size() == 1);

  // Check tools/call response
  REQUIRE(responses[2]["id"] == 3);
  REQUIRE(responses[2]["result"]["content"][0]["text"] == "test");
}

#endif  // ENABLE_MCP
