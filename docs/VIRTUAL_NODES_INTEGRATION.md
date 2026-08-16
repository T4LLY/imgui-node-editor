# Virtual Nodes Integration Guide

This document describes how to consume the large-graph extensions in this fork of
`imgui-node-editor` without changing the normal full-node rendering path.

The current implementation is intentionally incremental: applications keep their
existing node renderer, add a retained geometry cache, and submit off-screen nodes
through `SubmitVirtualNode()`.

## Status

Implemented in this fork:

- Dear ImGui compatibility through 1.92.9b.
- Headless compatibility/regression tests.
- Lightweight virtual node and pin submission.
- Retained pin geometry while virtual nodes move.
- Interaction candidate culling.
- Cached link geometry and visible-link filtering.
- ID lookup indexes, link adjacency indexes, and retained spatial buckets.
- Dirty-only Z-order sorting.

A virtual node is still submitted once per frame. Persistent virtual objects that
require no per-frame submission are a later roadmap item.

## Requirements

- Submit nodes before links, as with the normal API.
- A node must be measured by a full submission before it can be virtualized safely.
- The application must retain the node size and node-local pin bounds/pivots used by
  `VirtualNodeDesc`.
- A virtual descriptor must contain every pin needed by links or interaction in that
  frame.
- Native `Group()` nodes are not supported by the virtual path yet.

Unknown or unmeasured nodes are treated as visible by `IsNodeVisible()`, which makes
falling back to a full submission the safe default.

## Public API

The large-graph path adds these calls:

```cpp
bool SubmitVirtualNode(const VirtualNodeDesc& desc);
bool IsNodeVisible(NodeId id, float margin = 0.0f);
void GetVisibleCanvasBounds(ImVec2* min, ImVec2* max);
```

Descriptors use node-local coordinates:

```cpp
struct VirtualPinDesc
{
    PinId   Id;
    PinKind Kind;
    ImVec2  BoundsMinOffset;
    ImVec2  BoundsMaxOffset;
    ImVec2  PivotMinOffset;
    ImVec2  PivotMaxOffset;
};

struct VirtualNodeDesc
{
    NodeId                Id;
    ImVec2                Size;
    const VirtualPinDesc* Pins;
    int                   PinCount;
};
```

`SubmitVirtualNode()` returns `false` when the descriptor or submission state is
invalid. Treat that as a request to use a full submission on the next safe frame,
not as a reason to silently drop the node.

## Recommended frame flow

Use an overscan margin so nodes do not rapidly switch paths at the viewport edge.

```cpp
ed::Begin("Graph");

for (auto& node : graph.Nodes)
{
    auto& cache = geometryCache[node.Id];

    const bool needsFull =
        !cache.Valid ||
        cache.LayoutRevision != node.LayoutRevision ||
        ed::IsNodeVisible(node.Id, 128.0f);

    if (needsFull)
    {
        ed::BeginNode(node.Id);
        DrawNodeAndPins(node, cache); // refresh cache while layout is known
        ed::EndNode();
    }
    else
    {
        ed::VirtualNodeDesc desc;
        desc.Id       = node.Id;
        desc.Size     = cache.Size;
        desc.Pins     = cache.Pins.data();
        desc.PinCount = static_cast<int>(cache.Pins.size());

        if (!ed::SubmitVirtualNode(desc))
            cache.Valid = false;
    }
}

// Links come after both full and virtual nodes so all endpoint pins are live.
for (auto& link : graph.Links)
    ed::Link(link.Id, link.StartPin, link.EndPin, link.Color, link.Thickness);

ed::End();
```

If a failed virtual submission must be recovered in the same frame, structure the
application renderer so it can fall back to full submission before links are
submitted. Otherwise mark the cache invalid and recover on the next frame.

## Geometry cache

The application owns the descriptor cache. A practical cache is:

```cpp
struct CachedVirtualNode
{
    bool Valid = false;
    uint64_t LayoutRevision = 0;
    ImVec2 Size = {};
    std::vector<ed::VirtualPinDesc> Pins;
};
```

Store pin rectangles and pivots relative to the node origin, never in screen space.
That lets the editor translate retained pin geometry together with a dragged virtual
node.

This fork does not currently expose a public API that reconstructs
`VirtualNodeDesc` from an already-submitted node. The consumer should capture these
values from its own layout/pin geometry data while doing a full render. Applications
that already cache pin centers, hit rectangles, or node layout can usually derive the
descriptor directly from that data.

## Cache invalidation

Force a full submission when any input that can change node or pin geometry changes.
At minimum invalidate on:

- first appearance of a node;
- node type or dynamic port count change;
- pin insertion, removal, order, or kind change;
- node layout revision change;
- font, DPI, UI scale, or style change that affects geometry;
- title/label/content change that changes measured size;
- expand/collapse or LOD transition that changes layout;
- explicit application-side resize;
- restore/import when cached geometry is not known to match the restored layout.

Position changes do **not** require descriptor regeneration. The editor retains the
node position and translates virtual pin geometry with the node.

## Full, editorless, and virtual LOD

For applications that already have a lightweight on-screen renderer, use three
levels rather than replacing it:

```text
Full       visible + normal zoom        complete widgets/editors
Editorless visible + low zoom           simplified body, pins still submitted normally
Virtual    outside overscan             no ImGui node contents or per-node draw channels
```

Virtualization solves off-screen CPU submission cost. Editorless/LOD still matters
for many nodes that are simultaneously visible after zooming out.

## Links

Links must still be submitted each frame after their endpoint nodes/pins.

The fork caches each live link's Bezier geometry/bounds and uses retained spatial
buckets for visible-link queries and hover candidates. A link whose endpoints are
both outside the viewport is still considered when its curve crosses the viewport.

Do not application-cull a link solely because both endpoint nodes are off-screen.
If link submission itself becomes a bottleneck, persistent topology is a later
roadmap item.

## Interaction and selection

Virtual nodes remain live editor objects. Retained geometry is used for broad-phase
interaction, selection, and link endpoint updates.

The editor creates expensive ImGui interaction items only for nearby cursor
candidates and active drag/resize targets. Spatial buckets reduce the candidate set
before exact hit testing.

Selected virtual nodes can be moved without forcing a full render. Their retained
pin bounds and pivots move with the node, and adjacent link endpoints are refreshed.

## Draw-list behavior

A virtual node has no node draw channels. Therefore:

```cpp
ed::GetNodeBackgroundDrawList(virtualNodeId)
```

returns `nullptr`.

Custom decorations that require the node background draw list should either:

- be drawn only for full/editorless visible nodes; or
- use an application-level canvas layer that is not tied to a node draw channel.

Do not dereference the result without checking it.

## Native groups

`SubmitVirtualNode()` deliberately rejects native `Group()` nodes in the current
implementation. Keep groups fully submitted until group-specific retained geometry,
resize interaction, and group hints have dedicated coverage.

Applications with their own non-native visual grouping can virtualize the ordinary
member nodes normally.

## Rust / FFI consumers

A Rust consumer needs the new API exposed through every binding layer. For the
`dear-node-editor` stack used by TRAFFIQ, the expected path is:

```text
application
  -> dear-node-editor        safe Rust wrapper
  -> dear-node-editor-sys    generated/raw FFI
  -> C wrapper
  -> this imgui-node-editor fork
```

Expose POD equivalents of `VirtualPinDesc` and `VirtualNodeDesc`, plus:

- `SubmitVirtualNode`
- `IsNodeVisible`
- `GetVisibleCanvasBounds`

The safe wrapper should accept a borrowed pin slice and build a descriptor whose
pointer remains valid for the duration of the native call. Do not retain the Rust
slice pointer in C++; the current native implementation consumes descriptor data
synchronously.

For TRAFFIQ specifically, the existing cached node size and pin geometry used by its
editorless LOD should be reused instead of creating a second geometry model.

## Validation checklist

Before enabling virtualization by default, verify:

- full-only mode still behaves exactly as before;
- a newly-created node is full-submitted at least once;
- off-screen nodes remain selectable by rectangle selection;
- virtual selected nodes drag correctly;
- pins and links follow a moved virtual node;
- links crossing the viewport remain visible/hittable;
- link creation/deletion and context menus still work near viewport boundaries;
- settings/layout save and restore keep correct positions and sizes;
- zoom and pan across the full/virtual boundary do not cause jumps;
- dynamic-port nodes invalidate their geometry cache;
- group nodes remain on the full path;
- `GetNodeBackgroundDrawList()` callers tolerate `nullptr` for virtual nodes.

The repository's headless compatibility suite should also remain green with both the
bundled Dear ImGui and the currently supported external Dear ImGui version.

## Performance rollout

Enable the feature behind an application switch first. Compare identical graphs in:

1. full-only mode;
2. full + editorless LOD;
3. full + editorless + virtual nodes.

Measure at least idle, pan, zoom, node drag, link hover, and rectangle selection.
Test several visible ratios; a 10,000-node graph with 1% visible stresses a different
path than a 1,000-node graph with 90% visible.

The immediate success criterion is that off-screen node cost approaches the cost of
a descriptor submission rather than a normal `BeginNode`/pin/content path, without
changing editor behavior.
