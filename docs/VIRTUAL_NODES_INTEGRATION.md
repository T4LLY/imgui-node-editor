# Virtual Nodes Integration Guide

This document explains how to use the large-graph virtual-node path in this fork of
`imgui-node-editor` while keeping the normal full-node rendering path unchanged.

Virtual nodes are intended for nodes whose layout is already known but whose ImGui
contents do not need to be submitted because the node is outside the visible canvas.
The application retains the node size and pin geometry, then submits that retained
geometry through `SubmitVirtualNode()`.

## Status

Implemented in this fork:

- Dear ImGui compatibility through 1.92.9b.
- Headless compatibility/regression tests.
- Lightweight virtual node and pin submission.
- Retained pin geometry while virtual nodes move.
- Interaction candidate culling.
- Cached link geometry and visible-link filtering.
- ID lookup indexes, link adjacency indexes, and retained spatial buckets.
- Generation-based object lifetime tracking.
- Dirty-only Z-order sorting and redundant reorder avoidance.

A virtual node is still submitted once per frame. Persistent virtual topology that
requires no per-frame node/link submission is not implemented.

## Quick start

A typical application needs only this policy:

1. Fully submit a node while its retained geometry is unknown or invalid.
2. Cache the node size and every pin's bounds/pivot in node-local coordinates.
3. On later frames, fully submit visible nodes and virtually submit off-screen nodes.
4. Submit links only after all full and virtual nodes have been submitted.

`IsNodeVisible()` returns `true` for an unknown node, so this naturally sends a new
node through the full path first when the application does not already know its exact
geometry.

```cpp
ed::Begin("Graph");

for (auto& node : graph.Nodes)
{
    auto& cache = geometryCache[node.Id];

    const bool needsFull =
        !cache.Valid ||
        node.GeometryDirty ||
        ed::IsNodeVisible(node.Id, 128.0f);

    if (needsFull)
    {
        DrawFullNodeAndUpdateCache(node, cache);
        node.GeometryDirty = false;
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

// All endpoint pins must already be live before links are submitted.
for (auto& link : graph.Links)
    ed::Link(link.Id, link.StartPin, link.EndPin, link.Color, link.Thickness);

ed::End();
```

`GeometryDirty` in this example is application state, not an
`imgui-node-editor` API. A layout revision, dirty bit, or equivalent mechanism is
fine as long as the cache is invalidated whenever node or pin geometry changes.

If `SubmitVirtualNode()` returns `false`, do not silently omit that node forever.
Invalidate its cache and fully submit it on the next safe frame. An application that
needs same-frame recovery can perform the full fallback before it starts submitting
links.

## What must be cached

The application owns the virtual geometry cache. A minimal cache is:

```cpp
struct CachedVirtualNode
{
    bool Valid = false;
    ImVec2 Size = {};
    std::vector<ed::VirtualPinDesc> Pins;
};
```

Each `VirtualPinDesc` stores the same pin bounds and pivot geometry used by the full
renderer, but as offsets from the node origin:

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
```

The node descriptor is only a view of that retained data:

```cpp
struct VirtualNodeDesc
{
    NodeId                Id;
    ImVec2                Size;
    const VirtualPinDesc* Pins;
    int                   PinCount;
};
```

The pin array only has to remain valid for the duration of the
`SubmitVirtualNode()` call; the editor consumes the descriptor synchronously.

## Cache geometry during a full submission

There is intentionally no public `GetPinBounds()` or `GetPinPivot()` API. The
application should cache geometry from the same layout data it already uses while
fully rendering the node.

The clearest case is a renderer that explicitly supplies `PinRect()` and
`PinPivotRect()`. Keep those rectangles, then convert them to node-local offsets.

```cpp
struct FullPinGeometry
{
    ed::PinId Id;
    ed::PinKind Kind;
    ImVec2 BoundsMin;
    ImVec2 BoundsMax;
    ImVec2 PivotMin;
    ImVec2 PivotMax;
};

static ed::VirtualPinDesc MakeVirtualPin(
    const FullPinGeometry& pin,
    const ImVec2& nodeOrigin)
{
    ed::VirtualPinDesc result;
    result.Id               = pin.Id;
    result.Kind             = pin.Kind;
    result.BoundsMinOffset  = pin.BoundsMin - nodeOrigin;
    result.BoundsMaxOffset  = pin.BoundsMax - nodeOrigin;
    result.PivotMinOffset   = pin.PivotMin - nodeOrigin;
    result.PivotMaxOffset   = pin.PivotMax - nodeOrigin;
    return result;
}
```

A full renderer can then update the retained descriptor after `EndNode()`:

```cpp
void DrawFullNodeAndUpdateCache(Node& node, CachedVirtualNode& cache)
{
    std::vector<FullPinGeometry> measuredPins;

    ed::BeginNode(node.Id);

    // Draw the node contents normally. For each pin, retain the exact rectangles
    // used by PinRect()/PinPivotRect() in measuredPins.
    DrawNodeContents(node, measuredPins);

    ed::EndNode();

    const ImVec2 nodeOrigin = ed::GetNodePosition(node.Id);

    cache.Size = ed::GetNodeSize(node.Id);
    cache.Pins.clear();
    cache.Pins.reserve(measuredPins.size());

    for (const auto& pin : measuredPins)
        cache.Pins.push_back(MakeVirtualPin(pin, nodeOrigin));

    cache.Valid = true;
}
```

For example, when the full renderer already knows a pin rectangle and pivot:

```cpp
ed::BeginPin(pin.Id, pin.Kind);
DrawPinContents(pin);
ed::PinRect(pin.BoundsMin, pin.BoundsMax);
ed::PinPivotRect(pin.PivotMin, pin.PivotMax);
ed::EndPin();

measuredPins.push_back({
    pin.Id,
    pin.Kind,
    pin.BoundsMin,
    pin.BoundsMax,
    pin.PivotMin,
    pin.PivotMax,
});
```

Applications that let `EndPin()` infer the bounds from the last ImGui item instead
must retain equivalent geometry in their own renderer/layout cache. The current
public API does not expose the inferred internal pin rectangles after submission.

A full submission is therefore the normal bootstrap path when geometry comes from
ImGui measurement. It is not a hard API requirement if an application already knows
an exact node size and exact pin bounds/pivots from its own layout system.

## Coordinate rules

`VirtualPinDesc` coordinates are node-local offsets, not absolute canvas or screen
coordinates.

For a node at `nodeOrigin`:

```text
BoundsMinOffset = absolutePinBoundsMin - nodeOrigin
BoundsMaxOffset = absolutePinBoundsMax - nodeOrigin
PivotMinOffset  = absolutePinPivotMin  - nodeOrigin
PivotMaxOffset  = absolutePinPivotMax  - nodeOrigin
```

Do not regenerate the descriptor just because the node moves. The editor retains the
node position and translates the cached pin geometry with it.

`GetVisibleCanvasBounds()` returns the current visible rectangle in node-editor
canvas-local space. It is optional; use it when application-level culling needs the
same visible bounds used by the editor.

## Public API

The virtual-node path adds these calls:

```cpp
bool SubmitVirtualNode(const VirtualNodeDesc& desc);
bool IsNodeVisible(NodeId id, float margin = 0.0f);
void GetVisibleCanvasBounds(ImVec2* min, ImVec2* max);
```

### `SubmitVirtualNode()`

Submits retained node/pin geometry without emitting normal node contents or allocating
node draw channels. It returns `false` when the descriptor or current submission
state is invalid.

Important descriptor rules enforced by the implementation include:

- `Id` must be valid.
- `Size` must be finite and non-negative.
- `PinCount` must be non-negative.
- `Pins` must be non-null when `PinCount > 0`.
- every pin ID must be valid and unique within the descriptor;
- every pin kind must be `Input` or `Output`;
- all bounds/pivot coordinates must be finite and ordered min-to-max;
- the same node or pin cannot already have been submitted in that frame;
- native `Group()` nodes cannot use the virtual path.

### `IsNodeVisible()`

Tests the retained node bounds against the current view. `margin` expands the visible
rectangle, so a positive overscan such as `128.0f` prevents rapid switching at the
viewport edge.

Unknown or not-yet-sized nodes return `true`, which makes a full submission the safe
default.

### `GetVisibleCanvasBounds()`

Returns the current visible canvas rectangle. Either output pointer may be `nullptr`.
This is useful for application-side spatial queries, but it is not required for the
basic virtual-node flow.

## Cache invalidation

Force a full submission whenever an input that can change node or pin geometry
changes. Typical invalidation events include:

- first appearance of a node when exact geometry is not already known;
- node type or dynamic port count changes;
- pin insertion, removal, order, or kind changes;
- application layout revision changes;
- font, DPI, UI scale, or style changes that affect geometry;
- title, label, or content changes that change measured size;
- expand/collapse or application LOD transitions that change layout;
- explicit application-side resize;
- restore/import when cached geometry is not known to match the restored layout.

Position changes alone do **not** require descriptor regeneration.

## Links

Links must still be submitted each frame after all endpoint nodes/pins have been
submitted, whether those endpoints used the full or virtual path.

The fork caches live-link Bezier geometry/bounds and uses retained spatial buckets for
visible-link queries and hover candidates. A link whose endpoint nodes are both
off-screen is still considered when its curve crosses the viewport.

Do not application-cull a link solely because both endpoint nodes are off-screen.
Persistent topology that removes per-frame link submission is a separate deferred
optimization.

## Interaction and selection

Virtual nodes remain live editor objects. Retained geometry participates in
broad-phase interaction, selection, and link endpoint updates.

Selected virtual nodes can move without forcing a full render. Their retained pin
bounds/pivots move with the node, and adjacent link endpoints are refreshed.

The editor creates expensive ImGui interaction items only for nearby cursor
candidates and active drag/resize targets. Spatial buckets reduce the candidate set
before exact hit testing.

## Draw-list behavior

A virtual node has no node draw channels. Therefore:

```cpp
ed::GetNodeBackgroundDrawList(virtualNodeId)
```

returns `nullptr`.

Custom decorations that require the node background draw list should either be drawn
only for fully submitted visible nodes, or use an application-level canvas layer that
is not tied to a node draw channel. Always check the returned pointer before use.

## Native groups

`SubmitVirtualNode()` deliberately rejects native `Group()` nodes. Keep native groups
on the full path until retained group geometry, resize interaction, and group hints
have dedicated support.

Applications with their own non-native visual grouping can virtualize ordinary member
nodes normally.

## Application-side LOD

Virtual nodes solve off-screen submission cost. They do not reduce the cost of many
nodes that are simultaneously visible after zooming out.

An application may combine this API with its own visible-node LOD, for example:

```text
Full       visible + normal zoom        complete widgets/editors
Simplified visible + low zoom           application-defined lightweight contents
Virtual    outside overscan             retained geometry only
```

`Simplified`/`Editorless` rendering is an application strategy, not a separate
`imgui-node-editor` API. Any LOD that changes measured node or pin geometry must
invalidate the virtual geometry cache.

## FFI bindings

Bindings must expose POD equivalents of `VirtualPinDesc` and `VirtualNodeDesc`, plus:

- `SubmitVirtualNode`
- `IsNodeVisible`
- `GetVisibleCanvasBounds`

A safe wrapper can accept a borrowed pin slice and build a descriptor whose pointer
remains valid for the native call. The current C++ implementation consumes descriptor
data synchronously and does not retain the caller's pin-array pointer.

Reuse existing application layout geometry where possible instead of maintaining a
second independent geometry model for virtualization.

## Validation checklist

Before enabling virtualization by default, verify:

- full-only mode still behaves exactly as before;
- newly-created nodes reach the full path until valid geometry exists;
- off-screen nodes remain selectable by rectangle selection;
- selected virtual nodes drag correctly;
- pins and links follow a moved virtual node;
- links crossing the viewport remain visible/hittable;
- link creation/deletion and context menus still work near viewport boundaries;
- settings/layout save and restore keep correct positions and sizes;
- zoom and pan across the full/virtual boundary do not cause jumps;
- dynamic-port nodes invalidate their geometry cache;
- native group nodes remain on the full path;
- `GetNodeBackgroundDrawList()` callers tolerate `nullptr` for virtual nodes.

The repository's headless compatibility suite should remain green with both the
bundled Dear ImGui and the currently supported external Dear ImGui version.

## Performance rollout

Compare identical graphs with virtualization disabled and enabled. Measure at least:

- idle;
- pan and zoom;
- node drag;
- link hover;
- rectangle selection;
- several visible-node ratios.

A 10,000-node graph with 1% visible stresses a different path from a 1,000-node graph
with 90% visible. The useful criterion is that off-screen node cost approaches the
cost of descriptor submission while editor behavior remains unchanged.
