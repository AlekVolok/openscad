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
#include "MCPHandler.h"

// Forward declarations from OpenSCAD core
class SourceFile;
class AbstractNode;
class Geometry;

namespace openscad::mcp {

/**
 * Collected messages from OpenSCAD operations.
 */
struct CollectedMessage {
  std::string text;
  std::string group;  // "WARNING", "ERROR", etc.
  int line = 0;
  int column = 0;
};

/**
 * Result of parsing/compiling OpenSCAD code.
 */
struct CompileResult {
  bool success = false;
  std::vector<CollectedMessage> messages;
  std::shared_ptr<SourceFile> sourceFile;
  std::shared_ptr<AbstractNode> rootNode;
  std::shared_ptr<const Geometry> geometry;
};

/**
 * Compile OpenSCAD source code.
 * This is a shared utility used by the tools.
 */
CompileResult compileOpenSCAD(const std::string& code, bool evaluateGeometry = false);

/**
 * ValidateScadTool - Parse and validate OpenSCAD code.
 * Returns syntax errors and warnings without generating geometry.
 */
class ValidateScadTool : public MCPTool {
public:
  std::string name() const override { return "validate_scad"; }
  std::string description() const override {
    return "Parse and validate OpenSCAD code, returning any errors or warnings";
  }
  json inputSchema() const override;
  MCPToolResult execute(const json& params) override;
};

/**
 * RenderPreviewTool - Generate a PNG preview of OpenSCAD code.
 * Returns a base64-encoded PNG image.
 */
class RenderPreviewTool : public MCPTool {
public:
  std::string name() const override { return "render_preview"; }
  std::string description() const override {
    return "Render OpenSCAD code and return a PNG preview image";
  }
  json inputSchema() const override;
  MCPToolResult execute(const json& params) override;
};

/**
 * ExportModelTool - Export OpenSCAD code to various file formats.
 * Supports STL, OBJ, OFF, AMF, 3MF, DXF, SVG.
 */
class ExportModelTool : public MCPTool {
public:
  std::string name() const override { return "export_model"; }
  std::string description() const override {
    return "Export OpenSCAD code to a file format (STL, OBJ, OFF, AMF, 3MF, DXF, SVG)";
  }
  json inputSchema() const override;
  MCPToolResult execute(const json& params) override;
};

/**
 * Register all OpenSCAD tools with the handler.
 */
void registerOpenSCADTools(MCPHandler& handler);

}  // namespace openscad::mcp
