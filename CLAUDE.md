# OpenSCAD AI Agent Integration Plan

This document outlines a detailed plan for integrating AI agent capabilities into OpenSCAD, enabling Claude and other AI assistants to control the application for geometry generation and editing.

## Executive Summary

OpenSCAD's architecture is well-suited for AI agent integration due to its:
- Declarative scripting language with programmatic geometry generation
- Existing headless/CLI mode for batch processing
- Experimental Python integration
- Clean separation between language, geometry, and rendering layers
- External tool interface pattern

## Current Architecture Overview

### Core Components

| Layer | Purpose | Key Files |
|-------|---------|-----------|
| **Parser/Lexer** | OpenSCAD language processing | `src/core/parser.y`, `lexer.l` |
| **AST/Evaluation** | Expression evaluation, context management | `src/core/EvaluationSession.h`, `Context.h` |
| **Geometry Engine** | CSG operations, mesh generation | `src/geometry/GeometryEvaluator.h` |
| **Backends** | CGAL, Manifold geometry processing | `src/geometry/cgal/`, `src/geometry/manifold/` |
| **Rendering** | OpenGL visualization | `src/glview/VBORenderer.h` |
| **I/O** | File import/export (STL, DXF, SVG, etc.) | `src/io/export.h`, `import.h` |
| **GUI** | Qt-based interface | `src/gui/MainWindow.h` |

### Existing Extension Points

1. **CLI Mode** (`openscad.cc:795+`): Full batch processing with `-o`, `-D`, `-p` options
2. **Python Integration** (`src/python/`): Experimental Python bindings
3. **External Tool Interface** (`src/gui/ExternalToolInterface.h`): Abstract interface for tools
4. **Customizer System** (`src/core/customizer/`): Parameter-driven model customization

---

## Proposed Integration Approaches

### Approach 1: MCP Server (Recommended for Claude)

Create a Model Context Protocol (MCP) server that exposes OpenSCAD operations as tools.

#### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Claude / AI Client                       │
└─────────────────────────┬───────────────────────────────────┘
                          │ MCP Protocol (JSON-RPC over stdio/HTTP)
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   OpenSCAD MCP Server                        │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Tools:                                               │    │
│  │ • create_primitive(type, params) → scad_code        │    │
│  │ • apply_operation(op, objects) → combined_code      │    │
│  │ • render_preview(code) → PNG image                  │    │
│  │ • export_model(code, format) → file_path            │    │
│  │ • analyze_geometry(code) → {volume, bounds, etc.}   │    │
│  │ • modify_parameter(code, param, value) → new_code   │    │
│  │ • validate_code(code) → errors/warnings             │    │
│  └─────────────────────────────────────────────────────┘    │
│                          │                                   │
│                          ▼                                   │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ OpenSCAD Core (libOpenSCAD)                          │    │
│  │ • Parser → AST → Geometry → Export                   │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

#### Proposed MCP Tools

| Tool | Description | Parameters | Returns |
|------|-------------|------------|---------|
| `create_shape` | Generate primitive geometry | `type`, `dimensions`, `options` | OpenSCAD code |
| `combine_shapes` | Apply CSG operations | `operation`, `shapes[]` | Combined code |
| `transform` | Apply transformations | `code`, `translate/rotate/scale` | Transformed code |
| `render` | Generate preview image | `code`, `camera`, `size` | Base64 PNG |
| `export` | Export to file format | `code`, `format`, `path` | File path |
| `analyze` | Get geometry info | `code` | Volume, bounds, manifold status |
| `validate` | Check for errors | `code` | Errors, warnings |
| `customize` | Modify parameters | `code`, `params` | Modified code |
| `list_modules` | Get available modules | - | Module definitions |

#### Implementation Files

```
src/mcp/
├── MCPServer.h           # Main MCP server class
├── MCPServer.cc
├── MCPTransport.h        # stdio/HTTP transport
├── MCPTransport.cc
├── tools/
│   ├── ShapeTools.cc     # Primitive creation tools
│   ├── CSGTools.cc       # Boolean operation tools
│   ├── TransformTools.cc # Transformation tools
│   ├── RenderTools.cc    # Preview rendering tools
│   ├── ExportTools.cc    # File export tools
│   ├── AnalysisTools.cc  # Geometry analysis tools
│   └── ValidationTools.cc
└── CMakeLists.txt
```

### Approach 2: IPC/Socket Server Mode

Add a server mode to OpenSCAD that accepts commands via Unix socket or TCP.

#### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     OpenSCAD Process                         │
│                                                              │
│  ┌──────────────────┐    ┌─────────────────────────────┐    │
│  │  GUI (optional)  │◄──►│  IPCCommandServer           │    │
│  └──────────────────┘    │  (Unix socket / TCP:7878)   │    │
│          │               └──────────────┬──────────────┘    │
│          ▼                              │                    │
│  ┌──────────────────────────────────────▼──────────────┐    │
│  │              OpenSCAD Core Engine                    │    │
│  │  • Document management                               │    │
│  │  • Compilation & evaluation                          │    │
│  │  • Geometry generation                               │    │
│  │  • Export                                            │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                              │
                    Unix Socket / TCP
                              │
┌─────────────────────────────▼───────────────────────────────┐
│                   External Client                            │
│  (Claude Code, Python script, other tools)                  │
└─────────────────────────────────────────────────────────────┘
```

#### Command Protocol (JSON-RPC 2.0)

```json
// Request
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "document.setSource",
  "params": {
    "source": "cube([10, 10, 10]);"
  }
}

// Response
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "success": true,
    "documentId": "doc-1"
  }
}
```

#### Supported Commands

**Document Management:**
- `document.create` - Create new document
- `document.setSource` - Set OpenSCAD source code
- `document.getSource` - Get current source
- `document.close` - Close document

**Compilation & Rendering:**
- `compile` - Parse and evaluate current document
- `render.preview` - Generate preview (F5)
- `render.full` - Full geometry render (F6)
- `render.getImage` - Get rendered image

**Geometry Operations:**
- `geometry.analyze` - Get volume, bounding box, etc.
- `geometry.export` - Export to file format

**Editor Integration:**
- `editor.insert` - Insert text at position
- `editor.replace` - Replace text range
- `editor.getErrors` - Get syntax/semantic errors

### Approach 3: Enhanced Python Integration

Expand the existing Python integration for comprehensive control.

#### Current State

The existing `src/python/` directory contains experimental Python bindings:
- `pyopenscad.h/cc` - Python module interface
- `pyfunctions.cc` - Function bindings

#### Proposed Enhancements

```python
import openscad

# Create geometry programmatically
doc = openscad.Document()
doc.source = """
cube([10, 10, 10]);
sphere(r=5);
"""

# Or use Python API
cube = openscad.cube([10, 10, 10])
sphere = openscad.sphere(r=5)
combined = openscad.union(cube, sphere)

# Render and export
doc.compile()
doc.render()
doc.export("output.stl")

# Get geometry info
info = doc.analyze()
print(f"Volume: {info.volume}")
print(f"Bounds: {info.bounding_box}")

# Live preview integration
doc.show_preview()  # Opens preview window
```

#### Implementation Files

```
src/python/
├── pyopenscad.h          # Existing - enhance
├── pyopenscad.cc
├── pydocument.h          # Document class bindings
├── pydocument.cc
├── pygeometry.h          # Geometry class bindings
├── pygeometry.cc
├── pyprimitives.h        # Primitive creation functions
├── pyprimitives.cc
├── pyoperations.h        # CSG operations
├── pyoperations.cc
└── pyexport.h            # Export functions
```

### Approach 4: REST API Server

Add an HTTP REST API for web-based and remote control.

#### Endpoints

```
POST /api/v1/documents
  - Create new document
  - Body: { "source": "cube([10,10,10]);" }

PUT /api/v1/documents/{id}/source
  - Update document source

POST /api/v1/documents/{id}/compile
  - Compile document

GET /api/v1/documents/{id}/preview
  - Get preview image (PNG)

POST /api/v1/documents/{id}/export
  - Export to format
  - Body: { "format": "stl", "options": {...} }

GET /api/v1/documents/{id}/analysis
  - Get geometry analysis
```

---

## Recommended Implementation Plan

### Phase 1: MCP Server Foundation (Priority: High)

**Goal:** Basic MCP server that can generate and render OpenSCAD code.

#### Step 1.1: Core MCP Infrastructure
```
Files to create:
- src/mcp/MCPServer.h/cc        - Server main class
- src/mcp/MCPTransport.h/cc     - Transport layer (stdio)
- src/mcp/MCPHandler.h/cc       - Request/response handling
- src/mcp/MCPTypes.h            - Type definitions
```

#### Step 1.2: Basic Tools
```
Tools to implement:
- validate_scad    - Parse and check for errors
- render_preview   - Generate PNG preview
- export_model     - Export to STL/other formats
```

#### Step 1.3: Headless Integration
- Leverage existing headless mode (`HEADLESS=ON`)
- Use `OffscreenView` for preview generation
- Use existing export infrastructure

### Phase 2: Generative Tools (Priority: High)

**Goal:** Tools for AI to generate and manipulate geometry.

#### Step 2.1: Primitive Generation Tools
```
create_cube(size) → "cube([x, y, z]);"
create_sphere(radius) → "sphere(r=radius);"
create_cylinder(r, h) → "cylinder(r=radius, h=height);"
create_polyhedron(points, faces) → OpenSCAD code
```

#### Step 2.2: CSG Operation Tools
```
union(shapes[]) → "union() { ... }"
difference(a, b) → "difference() { ... }"
intersection(shapes[]) → "intersection() { ... }"
```

#### Step 2.3: Transformation Tools
```
translate(shape, vector) → "translate([x,y,z]) { ... }"
rotate(shape, angles) → "rotate([rx,ry,rz]) { ... }"
scale(shape, factors) → "scale([sx,sy,sz]) { ... }"
mirror(shape, plane) → "mirror([x,y,z]) { ... }"
```

### Phase 3: Analysis & Intelligence (Priority: Medium)

**Goal:** Tools for AI to understand and analyze geometry.

#### Step 3.1: Geometry Analysis
```
analyze_model(code) → {
  volume: number,
  surface_area: number,
  bounding_box: {min: [x,y,z], max: [x,y,z]},
  is_manifold: boolean,
  face_count: number,
  vertex_count: number
}
```

#### Step 3.2: Code Analysis
```
parse_model(code) → {
  modules: [...],
  parameters: [...],
  dependencies: [...]
}
```

#### Step 3.3: Customizer Integration
```
get_parameters(code) → [{name, type, value, range}]
set_parameters(code, params) → modified_code
```

### Phase 4: Live Interaction (Priority: Medium)

**Goal:** Real-time interaction with running OpenSCAD instance.

#### Step 4.1: IPC Server Mode
- Add `--server` flag to enable IPC mode
- Unix socket or TCP listener
- JSON-RPC protocol

#### Step 4.2: Document Control
- Open/close files
- Navigate viewport
- Set parameters

#### Step 4.3: Editor Integration
- Insert/modify code
- Get cursor position
- Get selection

### Phase 5: Advanced Features (Priority: Low)

**Goal:** Advanced AI-assisted capabilities.

#### Step 5.1: Semantic Understanding
- Parse OpenSCAD code into semantic representation
- Understand relationships between parts
- Suggest modifications

#### Step 5.2: Design Assistance
- Constraint-based design
- Fit analysis
- Assembly validation

---

## Technical Implementation Details

### MCP Server Implementation

#### Entry Point Integration

Modify `openscad.cc` to add MCP server mode:

```cpp
// In openscad_main()
if (vm.count("mcp-server")) {
    MCPServer server;
    server.run();  // Blocks, handles stdio
    return 0;
}
```

#### CMake Integration

```cmake
# In CMakeLists.txt
option(ENABLE_MCP "Enable MCP server support" ON)

if(ENABLE_MCP)
    add_subdirectory(src/mcp)
    target_link_libraries(OpenSCADLibInternal PRIVATE openscad_mcp)
endif()
```

#### MCP Server Class

```cpp
// src/mcp/MCPServer.h
class MCPServer {
public:
    MCPServer();
    void run();  // Main event loop

private:
    void handleInitialize(const json& params, json& result);
    void handleToolCall(const json& params, json& result);
    void handleListTools(json& result);

    std::unique_ptr<MCPTransport> transport_;
    std::map<std::string, std::unique_ptr<MCPTool>> tools_;

    // OpenSCAD integration
    std::unique_ptr<EvaluationSession> session_;
    std::unique_ptr<OffscreenView> renderer_;
};
```

#### Tool Interface

```cpp
// src/mcp/MCPTool.h
class MCPTool {
public:
    virtual ~MCPTool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json inputSchema() const = 0;
    virtual json execute(const json& params) = 0;
};

// Example: RenderTool
class RenderTool : public MCPTool {
public:
    std::string name() const override { return "render_preview"; }

    std::string description() const override {
        return "Render OpenSCAD code and return preview image";
    }

    json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"code", {{"type", "string"}, {"description", "OpenSCAD source code"}}},
                {"width", {{"type", "integer"}, {"default", 512}}},
                {"height", {{"type", "integer"}, {"default", 512}}},
                {"camera", {{"type", "object"}}}
            }},
            {"required", {"code"}}
        };
    }

    json execute(const json& params) override;
};
```

### Geometry Analysis Implementation

Leverage existing infrastructure:

```cpp
// src/mcp/tools/AnalysisTools.cc
json AnalysisTool::execute(const json& params) {
    std::string code = params["code"];

    // Parse and evaluate
    auto source = parse(code);
    EvaluationSession session;
    auto tree = session.evaluate(source);

    // Get geometry
    GeometryEvaluator evaluator;
    auto geometry = evaluator.evaluateGeometry(tree);

    // Analyze
    json result;
    if (auto polyset = dynamic_pointer_cast<PolySet>(geometry)) {
        result["vertex_count"] = polyset->vertices.size();
        result["face_count"] = polyset->indices.size() / 3;
        result["bounding_box"] = getBoundingBox(polyset);
    }

    if (auto manifold = dynamic_pointer_cast<ManifoldGeometry>(geometry)) {
        result["volume"] = manifold->volume();
        result["surface_area"] = manifold->surfaceArea();
        result["is_manifold"] = true;
    }

    return result;
}
```

---

## Configuration

### Claude Code MCP Configuration

Add to `~/.config/claude-code/settings.json` or project `.mcp.json`:

```json
{
  "mcpServers": {
    "openscad": {
      "command": "openscad",
      "args": ["--mcp-server"],
      "env": {}
    }
  }
}
```

### OpenSCAD Server Configuration

```json
{
  "server": {
    "transport": "stdio",
    "logLevel": "info"
  },
  "rendering": {
    "defaultWidth": 512,
    "defaultHeight": 512,
    "backend": "manifold"
  },
  "export": {
    "defaultFormat": "stl",
    "tempDirectory": "/tmp/openscad-mcp"
  }
}
```

---

## Example AI Interactions

### Example 1: Create a Simple Part

```
User: Create a mounting bracket with two holes

Claude uses tools:
1. create_shape("cube", {size: [50, 30, 5]})
2. create_shape("cylinder", {r: 3, h: 10})
3. transform("translate", shape, [-15, 0, -2.5])
4. transform("translate", shape, [15, 0, -2.5])
5. combine_shapes("difference", [base, hole1, hole2])
6. render_preview(combined_code)
```

### Example 2: Modify Existing Design

```
User: Make the bracket thicker and add a third hole

Claude uses tools:
1. parse_model(current_code)
2. modify_parameter(code, "thickness", 8)
3. create_shape("cylinder", {r: 3, h: 12})
4. combine_shapes("difference", [current, new_hole])
5. render_preview(new_code)
```

### Example 3: Analyze and Export

```
User: What's the volume? Export as STL if it's under 100cm³

Claude uses tools:
1. analyze_model(code) → {volume: 85.3}
2. export_model(code, "stl", "bracket.stl")
```

---

## Security Considerations

1. **Sandboxing**: MCP server runs with limited filesystem access
2. **Resource Limits**: Timeout on geometry operations, memory limits
3. **Input Validation**: Sanitize all code inputs
4. **Temp File Cleanup**: Auto-cleanup of rendered images and exports

---

## Dependencies

### Required
- nlohmann/json (already in `src/ext/json/`)
- Existing OpenSCAD headless mode infrastructure

### Optional
- cpp-httplib (for REST API approach)
- libzmq (for advanced IPC)

---

## Testing Strategy

### Unit Tests
```
tests/mcp/
├── test_mcp_server.cc
├── test_shape_tools.cc
├── test_csg_tools.cc
├── test_render_tools.cc
└── test_export_tools.cc
```

### Integration Tests
```
tests/mcp/integration/
├── test_full_workflow.py
├── test_claude_integration.py
└── fixtures/
    ├── simple_shapes.scad
    └── complex_model.scad
```

---

## Milestones and Deliverables

### Milestone 1: MVP (Foundation)
- [ ] MCP server infrastructure
- [ ] Basic stdio transport
- [ ] `validate_scad` tool
- [ ] `render_preview` tool
- [ ] `export_model` tool

### Milestone 2: Generation
- [ ] Shape creation tools (primitives)
- [ ] CSG operation tools
- [ ] Transformation tools
- [ ] Code generation helpers

### Milestone 3: Intelligence
- [ ] Geometry analysis tools
- [ ] Code parsing/analysis
- [ ] Customizer integration
- [ ] Parameter modification

### Milestone 4: Live Control
- [ ] IPC server mode
- [ ] Document management
- [ ] Viewport control
- [ ] Real-time updates

---

## Conclusion

The MCP-based approach provides the most practical path for Claude integration because:

1. **Standard Protocol**: MCP is Claude's native tool integration protocol
2. **Minimal Invasiveness**: Can be built as optional module
3. **Headless Support**: Leverages existing headless infrastructure
4. **Extensible**: Easy to add new tools incrementally
5. **Secure**: Natural sandboxing through process isolation

The implementation builds on OpenSCAD's existing architecture, particularly the headless mode, evaluation system, and export infrastructure, making it feasible without major core changes.
