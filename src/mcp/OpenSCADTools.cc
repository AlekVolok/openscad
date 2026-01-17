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

#include "OpenSCADTools.h"
#include "openscad.h"
#include "core/Builtins.h"
#include "core/BuiltinContext.h"
#include "core/EvaluationSession.h"
#include "core/SourceFile.h"
#include "core/Tree.h"
#include "core/node.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryEvaluator.h"
#include "geometry/GeometryUtils.h"
#include "geometry/PolySet.h"
#include "glview/Camera.h"
#include "glview/OffscreenView.h"
#include "io/export.h"
#include "utils/printutils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>

namespace fs = std::filesystem;

namespace openscad::mcp {

// Thread-safe message collector
class MessageCollector {
public:
  MessageCollector() {
    // Save existing handler
    oldHandler_ = outputhandler;
    oldData_ = outputhandler_data;

    // Set our handler
    set_output_handler(&MessageCollector::handler, nullptr, this);
  }

  ~MessageCollector() {
    // Restore old handler
    set_output_handler(oldHandler_, nullptr, oldData_);
  }

  std::vector<CollectedMessage> getMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
  }

  bool hasErrors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& msg : messages_) {
      if (msg.group == "ERROR") return true;
    }
    return false;
  }

private:
  static void handler(const Message& msgObj, void* userdata) {
    auto* self = static_cast<MessageCollector*>(userdata);
    std::lock_guard<std::mutex> lock(self->mutex_);

    CollectedMessage collected;
    collected.text = msgObj.msg;
    collected.group = getGroupName(msgObj.group);

    if (!msgObj.loc.isNone()) {
      collected.line = msgObj.loc.firstLine();
      collected.column = msgObj.loc.firstColumn();
    }

    self->messages_.push_back(collected);
  }

  mutable std::mutex mutex_;
  std::vector<CollectedMessage> messages_;
  OutputHandlerFunc* oldHandler_ = nullptr;
  void* oldData_ = nullptr;
};

// Ensure builtins are initialized (thread-safe, only runs once)
static void ensureBuiltinsInitialized() {
  static std::once_flag initFlag;
  std::call_once(initFlag, []() {
    Builtins::instance()->initialize();
  });
}

CompileResult compileOpenSCAD(const std::string& code, bool evaluateGeometry) {
  CompileResult result;

  ensureBuiltinsInitialized();

  // Collect messages during compilation
  MessageCollector collector;

  // Create a temporary working directory
  fs::path tempDir = fs::temp_directory_path() / "openscad-mcp";
  fs::create_directories(tempDir);

  // Parse the code
  SourceFile* sourceFile = nullptr;
  if (!parse(sourceFile, code, "<mcp-input>", "<mcp-input>", 0)) {
    result.messages = collector.getMessages();
    if (result.messages.empty()) {
      result.messages.push_back({"Parse error: Unknown error during parsing", "ERROR", 0, 0});
    }
    return result;
  }

  if (!sourceFile) {
    result.messages.push_back({"Parse error: Failed to create source file", "ERROR", 0, 0});
    return result;
  }

  result.sourceFile.reset(sourceFile);

  // Create evaluation session
  EvaluationSession session{tempDir.string()};
  ContextHandle<BuiltinContext> builtinContext{Context::create<BuiltinContext>(&session)};

  // Instantiate the root node
  AbstractNode::resetIndexCounter();
  std::shared_ptr<const FileContext> fileContext;

  try {
    result.rootNode = sourceFile->instantiate(*builtinContext, &fileContext);
  } catch (const std::exception& e) {
    result.messages = collector.getMessages();
    result.messages.push_back({std::string("Evaluation error: ") + e.what(), "ERROR", 0, 0});
    return result;
  }

  if (!result.rootNode) {
    result.messages = collector.getMessages();
    result.messages.push_back({"Evaluation error: Failed to instantiate module", "ERROR", 0, 0});
    return result;
  }

  // If geometry evaluation is requested
  if (evaluateGeometry) {
    Tree tree(result.rootNode, tempDir.string());
    GeometryEvaluator geomEvaluator(tree);

    try {
      constexpr bool allowNef = true;
      result.geometry = geomEvaluator.evaluateGeometry(*tree.root(), allowNef);

      if (!result.geometry) {
        result.geometry = std::make_shared<PolySet>(3);
      }
    } catch (const std::exception& e) {
      result.messages = collector.getMessages();
      result.messages.push_back({std::string("Geometry error: ") + e.what(), "ERROR", 0, 0});
      return result;
    }
  }

  result.messages = collector.getMessages();
  result.success = !collector.hasErrors();

  return result;
}

// ============================================================================
// ValidateScadTool
// ============================================================================

json ValidateScadTool::inputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"code", {
        {"type", "string"},
        {"description", "OpenSCAD source code to validate"}
      }}
    }},
    {"required", json::array({"code"})}
  };
}

MCPToolResult ValidateScadTool::execute(const json& params) {
  MCPToolResult result;

  if (!params.contains("code") || !params["code"].is_string()) {
    result.isError = true;
    result.content.push_back(TextContent{"Missing required parameter: code"});
    return result;
  }

  std::string code = params["code"];
  CompileResult compileResult = compileOpenSCAD(code, false);

  // Build response
  json response;
  response["valid"] = compileResult.success;
  response["message_count"] = compileResult.messages.size();

  json messagesJson = json::array();
  for (const auto& msg : compileResult.messages) {
    json msgJson;
    msgJson["text"] = msg.text;
    msgJson["type"] = msg.group;
    if (msg.line > 0) {
      msgJson["line"] = msg.line;
      msgJson["column"] = msg.column;
    }
    messagesJson.push_back(msgJson);
  }
  response["messages"] = messagesJson;

  result.content.push_back(TextContent{response.dump(2)});
  return result;
}

// ============================================================================
// RenderPreviewTool
// ============================================================================

json RenderPreviewTool::inputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"code", {
        {"type", "string"},
        {"description", "OpenSCAD source code to render"}
      }},
      {"width", {
        {"type", "integer"},
        {"description", "Image width in pixels"},
        {"default", 512},
        {"minimum", 64},
        {"maximum", 4096}
      }},
      {"height", {
        {"type", "integer"},
        {"description", "Image height in pixels"},
        {"default", 512},
        {"minimum", 64},
        {"maximum", 4096}
      }}
    }},
    {"required", json::array({"code"})}
  };
}

MCPToolResult RenderPreviewTool::execute(const json& params) {
  MCPToolResult result;

  if (!params.contains("code") || !params["code"].is_string()) {
    result.isError = true;
    result.content.push_back(TextContent{"Missing required parameter: code"});
    return result;
  }

  std::string code = params["code"];
  int width = params.value("width", 512);
  int height = params.value("height", 512);

  // Clamp dimensions
  width = std::clamp(width, 64, 4096);
  height = std::clamp(height, 64, 4096);

  // Compile with geometry evaluation
  CompileResult compileResult = compileOpenSCAD(code, true);

  if (!compileResult.success || !compileResult.geometry) {
    result.isError = true;
    std::string errorMsg = "Compilation failed:\n";
    for (const auto& msg : compileResult.messages) {
      errorMsg += msg.group + ": " + msg.text + "\n";
    }
    result.content.push_back(TextContent{errorMsg});
    return result;
  }

  // Set up camera
  Camera camera;
  camera.autocenter = true;
  camera.viewall = true;

  // Set up view options
  ViewOptions viewOptions;
  viewOptions.renderer = RenderType::GEOMETRY;

  // Render to PNG
  std::ostringstream pngStream;
  bool success = false;

  try {
    success = export_png(compileResult.geometry, viewOptions, camera, pngStream);
  } catch (const std::exception& e) {
    result.isError = true;
    result.content.push_back(TextContent{std::string("Render error: ") + e.what()});
    return result;
  }

  if (!success) {
    result.isError = true;
    result.content.push_back(TextContent{"Failed to render PNG"});
    return result;
  }

  // Encode as base64
  std::string pngData = pngStream.str();
  static const char base64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string base64;
  base64.reserve(((pngData.size() + 2) / 3) * 4);

  for (size_t i = 0; i < pngData.size(); i += 3) {
    unsigned int n = (static_cast<unsigned char>(pngData[i]) << 16);
    if (i + 1 < pngData.size()) n |= (static_cast<unsigned char>(pngData[i + 1]) << 8);
    if (i + 2 < pngData.size()) n |= static_cast<unsigned char>(pngData[i + 2]);

    base64 += base64Chars[(n >> 18) & 0x3F];
    base64 += base64Chars[(n >> 12) & 0x3F];
    base64 += (i + 1 < pngData.size()) ? base64Chars[(n >> 6) & 0x3F] : '=';
    base64 += (i + 2 < pngData.size()) ? base64Chars[n & 0x3F] : '=';
  }

  result.content.push_back(ImageContent{base64, "image/png"});
  return result;
}

// ============================================================================
// ExportModelTool
// ============================================================================

json ExportModelTool::inputSchema() const {
  return {
    {"type", "object"},
    {"properties", {
      {"code", {
        {"type", "string"},
        {"description", "OpenSCAD source code to export"}
      }},
      {"format", {
        {"type", "string"},
        {"description", "Export format"},
        {"enum", json::array({"stl", "obj", "off", "amf", "3mf", "dxf", "svg"})},
        {"default", "stl"}
      }},
      {"binary", {
        {"type", "boolean"},
        {"description", "Use binary format (for STL)"},
        {"default", true}
      }}
    }},
    {"required", json::array({"code"})}
  };
}

MCPToolResult ExportModelTool::execute(const json& params) {
  MCPToolResult result;

  if (!params.contains("code") || !params["code"].is_string()) {
    result.isError = true;
    result.content.push_back(TextContent{"Missing required parameter: code"});
    return result;
  }

  std::string code = params["code"];
  std::string format = params.value("format", "stl");
  bool binary = params.value("binary", true);

  // Compile with geometry evaluation
  CompileResult compileResult = compileOpenSCAD(code, true);

  if (!compileResult.success || !compileResult.geometry) {
    result.isError = true;
    std::string errorMsg = "Compilation failed:\n";
    for (const auto& msg : compileResult.messages) {
      errorMsg += msg.group + ": " + msg.text + "\n";
    }
    result.content.push_back(TextContent{errorMsg});
    return result;
  }

  // Check geometry dimension
  int geomDim = compileResult.geometry->getDimension();

  // Export to string stream
  std::ostringstream exportStream;
  bool success = false;

  try {
    if (format == "stl") {
      if (geomDim != 3) {
        result.isError = true;
        result.content.push_back(TextContent{"STL export requires 3D geometry"});
        return result;
      }
      export_stl(compileResult.geometry, exportStream, binary);
      success = true;
    } else if (format == "obj") {
      if (geomDim != 3) {
        result.isError = true;
        result.content.push_back(TextContent{"OBJ export requires 3D geometry"});
        return result;
      }
      export_obj(compileResult.geometry, exportStream);
      success = true;
    } else if (format == "off") {
      if (geomDim != 3) {
        result.isError = true;
        result.content.push_back(TextContent{"OFF export requires 3D geometry"});
        return result;
      }
      export_off(compileResult.geometry, exportStream);
      success = true;
    } else if (format == "amf") {
      if (geomDim != 3) {
        result.isError = true;
        result.content.push_back(TextContent{"AMF export requires 3D geometry"});
        return result;
      }
      export_amf(compileResult.geometry, exportStream);
      success = true;
    } else if (format == "dxf") {
      if (geomDim != 2) {
        result.isError = true;
        result.content.push_back(TextContent{"DXF export requires 2D geometry"});
        return result;
      }
      export_dxf(compileResult.geometry, exportStream);
      success = true;
    } else if (format == "svg") {
      if (geomDim != 2) {
        result.isError = true;
        result.content.push_back(TextContent{"SVG export requires 2D geometry"});
        return result;
      }
      ExportInfo exportInfo;
      exportInfo.format = FileFormat::SVG;
      exportInfo.optionsSvg = ExportSvgOptions::fromSettings();
      export_svg(compileResult.geometry, exportStream, exportInfo);
      success = true;
    } else if (format == "3mf") {
      if (geomDim != 3) {
        result.isError = true;
        result.content.push_back(TextContent{"3MF export requires 3D geometry"});
        return result;
      }
      ExportInfo exportInfo;
      exportInfo.format = FileFormat::_3MF;
      exportInfo.options3mf = Export3mfOptions::fromSettings();
      export_3mf(compileResult.geometry, exportStream, exportInfo);
      success = true;
    } else {
      result.isError = true;
      result.content.push_back(TextContent{"Unknown export format: " + format});
      return result;
    }
  } catch (const std::exception& e) {
    result.isError = true;
    result.content.push_back(TextContent{std::string("Export error: ") + e.what()});
    return result;
  }

  if (!success) {
    result.isError = true;
    result.content.push_back(TextContent{"Failed to export model"});
    return result;
  }

  // For binary formats, encode as base64
  std::string exportData = exportStream.str();

  if (binary && (format == "stl" || format == "3mf" || format == "amf")) {
    // Base64 encode
    static const char base64Chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string base64;
    base64.reserve(((exportData.size() + 2) / 3) * 4);

    for (size_t i = 0; i < exportData.size(); i += 3) {
      unsigned int n = (static_cast<unsigned char>(exportData[i]) << 16);
      if (i + 1 < exportData.size()) n |= (static_cast<unsigned char>(exportData[i + 1]) << 8);
      if (i + 2 < exportData.size()) n |= static_cast<unsigned char>(exportData[i + 2]);

      base64 += base64Chars[(n >> 18) & 0x3F];
      base64 += base64Chars[(n >> 12) & 0x3F];
      base64 += (i + 1 < exportData.size()) ? base64Chars[(n >> 6) & 0x3F] : '=';
      base64 += (i + 2 < exportData.size()) ? base64Chars[n & 0x3F] : '=';
    }

    json response;
    response["format"] = format;
    response["encoding"] = "base64";
    response["data"] = base64;
    response["size_bytes"] = exportData.size();

    result.content.push_back(TextContent{response.dump()});
  } else {
    // Text formats can be returned directly
    json response;
    response["format"] = format;
    response["encoding"] = "text";
    response["data"] = exportData;
    response["size_bytes"] = exportData.size();

    result.content.push_back(TextContent{response.dump()});
  }

  return result;
}

// ============================================================================
// Tool Registration
// ============================================================================

void registerOpenSCADTools(MCPHandler& handler) {
  handler.registerTool(std::make_unique<ValidateScadTool>());
  handler.registerTool(std::make_unique<RenderPreviewTool>());
  handler.registerTool(std::make_unique<ExportModelTool>());
}

}  // namespace openscad::mcp
