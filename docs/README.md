# Node Editor in ImGui

[![build](https://github.com/thedmd/imgui-node-editor/actions/workflows/build.yml/badge.svg)](https://github.com/thedmd/imgui-node-editor/actions/workflows/build.yml)

## About

An implementation of node editor with ImGui-like API.

Project purpose is to serve as a basis for more complex solutions like blueprint editors.

![node_editor_overview](https://user-images.githubusercontent.com/1197433/89328475-c01bc680-d68d-11ea-88bf-8c4155480927.gif)

Node Editor is build around an idea "draw your content, we do the rest", which mean interactions are handled by editor, content rendering is handled by user. Editor will take care of:
 * placing your node in the word
 * dragging nodes
 * zoom and scrolling
 * selection
 * various interaction that can be queried by API (creation, deletion, selection changes, etc.)

Here are some highlights:
 * Node movement and selection is handled internally
 * Node and pin contents are fully customizable
 * Fully styled, default theme is modeled after UE4 blueprints
    - Flexible enough to produce such nodes:

        ![image](https://user-images.githubusercontent.com/1197433/60381408-c3895b00-9a54-11e9-8312-d9fc9af63347.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381400-a3599c00-9a54-11e9-9c51-a88f25f7db07.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381589-7d81c680-9a57-11e9-87b1-9f73ec33bea4.png)
    - Customizable links based on Bézier curves:

        ![image](https://user-images.githubusercontent.com/1197433/60381475-ac973880-9a55-11e9-9ad9-5862975cd2b8.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381467-9db08600-9a55-11e9-9868-2ae849f67de9.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381488-cd5f8e00-9a55-11e9-8346-1f4c8d6bea22.png)
 * Automatic highlights for nodes, pins and links:

    ![image](https://user-images.githubusercontent.com/1197433/60381536-9e95e780-9a56-11e9-80bb-dad0d3d9557a.png)
 * Smooth navigation and selection
 * Node state can be saved in user context, so layout will not break
 * Selection rectangles and group dragging
 * Context menu support
 * Basic shortcuts support (cut/copy/paste/delete)
 * ImGui style API

Editor is used to implement blueprint editor in Spark CE engine, it proved itself there by allowing to do everything we needed. Therefore it is now slowly moving into stable state from beeing a prototype.

Note: Project recently was restructured to mimic ImGui layout.

Please report issues or questions if something isn't clear.

## Dependencies

 * Vanilla ImGui 1.72+ (tested through 1.92.9b)
 * C++14

### Dependencies for examples:
 * https://github.com/thedmd/imgui/tree/feature/layout (used in blueprints sample only)

### Optional extension you can pull into your local copy of ImGui node editor can take advantage of:
 * ~~https://github.com/thedmd/imgui/tree/feature/draw-list-fringe-scale (for sharp rendering, while zooming)~~ It is part of ImGui since 1.80 release
 * https://github.com/thedmd/imgui/tree/feature/extra-keys (for extra shortcuts)

## Building / Installing

Node Editor sources are located in root project directory. To use it, simply copy&paste sources into your project. Exactly like you can do with ImGui.

### Examples
[Examples](../examples) can be build with CMake:
```
Windows:
    cmake -S examples -B build -G "Visual Studio 15 2017 Win64"
      or
    cmake -S examples -B build -G "Visual Studio 16 2019" -A x64

macOS:
    cmake -S examples -B build -G "Xcode"

Linux:
    cmake -S examples -B build -G "Unix Makefiles"

Build:
    cmake --build build --config Release
```
Executables will be located in `build\bin` directory.

### Compatibility tests

The headless compatibility suite exercises node/link submission, selection, node position state,
groups, suspend/resume, navigation transforms, programmatic deletion, settings restore, and
repeated-frame lifetime behavior. It can be built against the bundled ImGui or an external
Dear ImGui source tree.

```
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure

# Test another Dear ImGui checkout, for example 1.92.9b:
cmake -S tests -B build-tests-1.92.9b -DCMAKE_BUILD_TYPE=Release \
  -DIMGUI_NODE_EDITOR_IMGUI_DIR=/path/to/imgui
cmake --build build-tests-1.92.9b --config Release
ctest --test-dir build-tests-1.92.9b -C Release --output-on-failure
```

CI runs this suite against both the bundled ImGui and Dear ImGui 1.92.9b on Windows, Linux,
and macOS.

### Virtual nodes

Large graphs can keep off-screen nodes live without rebuilding their ImGui contents every frame.
Cache node-local pin bounds/pivots after a measured full submission, then use `SubmitVirtualNode()`
for nodes outside the visible canvas. Submit all nodes before links, exactly as with full nodes.

```cpp
if (ed::IsNodeVisible(nodeId, overscan))
{
    ed::BeginNode(nodeId);
    // Draw contents and pins.
    ed::EndNode();
}
else
{
    ed::VirtualNodeDesc node = {/* cached size and node-local pin geometry */};
    ed::SubmitVirtualNode(node);
}
```

`GetNodeBackgroundDrawList()` returns `nullptr` for a virtual node because no node draw channels
are allocated. Native `Group()` nodes are intentionally not virtualized by this initial API.

Interaction hit testing remains geometry-driven: retained node and link bounds are indexed in spatial
buckets, so only nearby candidates are considered before exact hit testing. Only the cursor candidate
and active drag/size target emit ImGui interaction items. Retained pin bounds and pivots move with
their node, so selected virtual nodes can move without rebuilding their contents.

Node, pin, and link ID lookup plus current-frame link adjacency are indexed internally. This requires
no additional application API and removes several whole-graph scans from interaction, drag, and link
queries on large graphs.

### Quick Start

Main node editor header is located in [imgui_node_editor.h](../imgui_node_editor.h).

Minimal example of one node can be found in [simple-example.cpp](../examples/simple-example/simple-example.cpp).
Press 'F' in editor to focus on editor content if you see only grid.
```cpp
# include <imgui.h>
# include <imgui_node_editor.h>
# include <application.h>

namespace ed = ax::NodeEditor;

struct Example:
    public Application
{
    using Application::Application;

    void OnStart() override
    {
        ed::Config config;
        config.SettingsFile = "Simple.json";
        m_Context = ed::CreateEditor(&config);
    }

    void OnStop() override
    {
        ed::DestroyEditor(m_Context);
    }

    void OnFrame(float deltaTime) override
    {
        auto& io = ImGui::GetIO();

        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

        ImGui::Separator();

        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));
        int uniqueId = 1;
        // Start drawing nodes.
        ed::BeginNode(uniqueId++);
            ImGui::Text("Node A");
            ed::BeginPin(uniqueId++, ed::PinKind::Input);
                ImGui::Text("-> In");
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin(uniqueId++, ed::PinKind::Output);
                ImGui::Text("Out ->");
            ed::EndPin();
        ed::EndNode();
        ed::End();
        ed::SetCurrentEditor(nullptr);

        //ImGui::ShowMetricsWindow();
    }

    ed::EditorContext* m_Context = nullptr;
};

int Main(int argc, char** argv)
{
    Example exampe("Simple", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}
```

Result:

![00-Simple](https://user-images.githubusercontent.com/1197433/89328516-cca01f00-d68d-11ea-9959-2da159851101.png)

For more details please visit [examples](../examples) folder.

### Blueprints Example

![Preview2](https://user-images.githubusercontent.com/1197433/60053458-2f2b9b00-96d8-11e9-92f9-08aff63b2023.png)

### Here is Node Editor at work in Spark CE
![image](https://user-images.githubusercontent.com/1197433/60381756-174a7300-9a5a-11e9-9a04-00f10565e05e.png)
![image](https://user-images.githubusercontent.com/1197433/60381760-2f21f700-9a5a-11e9-9053-c0547a9cc40a.png)
