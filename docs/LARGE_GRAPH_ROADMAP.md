# Large-Graph Roadmap

This roadmap starts from the currently working virtual-node implementation. The goal
is to improve large-graph CPU scaling without turning `imgui-node-editor` into a new
retained GUI framework or destabilizing the normal API.

## Current baseline

The following work is already present in this fork:

### Compatibility and safety

- Dear ImGui compatibility through 1.92.9b.
- Headless regression/compatibility suite.
- CI coverage for bundled and external Dear ImGui.

### Virtual-node path

- Full vs virtual node submission state.
- `SubmitVirtualNode()` retained node/pin geometry.
- `IsNodeVisible()` and visible canvas bounds.
- No per-node draw channels for virtual nodes.
- Retained pin geometry follows virtual-node movement.
- Virtual node/pin interaction remains available.

### Large-graph broad phase

- Interaction candidate culling by retained bounds.
- Cached link Bezier geometry and bounds.
- Visible-link candidate filtering.
- O(1)-style ID lookup maps for nodes, pins, links, and settings.
- Node/pin link adjacency indexes.
- Retained node/link spatial buckets.
- Dirty-only Z-order sorting.

This is the first practical integration point. Consumer integration and real-world
measurement should happen before deeper architectural work.

## Phase 8 — Benchmark and instrumentation

**Priority: highest. Do this before speculative optimization.**

Add a benchmark/example that can generate deterministic graphs at roughly:

```text
100
1,000
10,000 nodes
```

with configurable link density and visible ratio.

Measure separately:

- editor `Begin()` / reset;
- full node submission;
- virtual node submission;
- pin submission;
- `BuildControl()` / interaction broad phase;
- spatial queries/rebuilds;
- link submission and endpoint updates;
- visible-link rebuild;
- draw-channel growth/reorder/merge;
- editor `End()` total.

Scenarios:

- idle;
- pan;
- zoom;
- one-node drag;
- multi-node drag;
- link hover;
- rectangle selection;
- topology edit;
- 100%, 10%, and 1% visible.

Prefer stable counters/timers that can run in CI or a benchmark executable over
profiling hooks embedded permanently in the public API.

### Exit criterion

A profile identifies the dominant remaining costs for 1k and 10k graphs. Later
phases should be justified by those measurements.

## Phase 9 — Incremental spatial-index updates

**Priority: high if drag/topology benchmarks show rebuild spikes.**

The current spatial index is retained but rebuilt globally after geometry is marked
dirty. Moving one node can therefore cause a later query to rebuild all indexed
nodes; moving adjacent link endpoints can similarly invalidate the link index.

Replace coarse dirty/rebuild behavior with incremental membership updates:

```text
object
  -> previous spatial cells
  -> remove from old cells
  -> insert into new cells
```

Update only:

- the moved/resized node;
- its pins indirectly through node geometry;
- links adjacent to that node;
- added/removed objects.

Keep a safe full-rebuild path for bulk restore/import and validation/debug builds.

### Things to verify

- objects crossing negative cell coordinates;
- very large objects using the overflow path;
- cell-list removal without dangling pointers;
- group/resize behavior;
- multi-node drag;
- links changing endpoint pins under the same LinkId.

### Exit criterion

Dragging one node in a 10k-node graph no longer produces work proportional to all
nodes/links solely because of spatial-index maintenance.

## Phase 10 — Persistent virtual objects

**Priority: high only when per-frame virtual submission becomes measurable.**

Today virtual nodes are lightweight, but the application still calls
`SubmitVirtualNode()` for every off-screen node every frame and submits the pins in
the descriptor. That keeps frame cost O(total nodes + total virtual pins).

Introduce an optional persistent topology/geometry API, for example conceptually:

```cpp
RegisterVirtualNode(...);   // create once
UpdateVirtualNode(...);     // only when geometry/topology changes
RemoveVirtualNode(...);
```

or an equivalent generation-based contract.

The important semantic distinction is:

```text
not submitted this frame != dead
```

A persistent virtual object should remain available to selection, navigation, links,
and spatial queries until explicitly invalidated or removed.

### Design constraints

- normal immediate-mode `BeginNode()` must remain supported;
- full submission should override/update retained geometry safely;
- object lifetime must be explicit and debuggable;
- stale pins/links must not survive topology changes;
- save/restore semantics must remain compatible;
- application code should not need to own editor-internal pointers.

### Exit criterion

An unchanged off-screen node has effectively zero application/native submission work
for that frame beyond whatever visible-query bookkeeping remains.

## Phase 11 — Generation-based lifetime and hot lists

**Priority: medium; pair with Phase 10 if measurements support it.**

Several frame-start/end operations still scale with total retained object count.
Candidates include:

- reset/collect passes over all nodes, pins, and links;
- clearing current-frame adjacency vectors/maps;
- scans that only need fully-submitted or selected objects;
- rebuilding temporary full-node lists.

Possible mechanisms:

- `last_seen_frame` / generation counters instead of eager reset;
- separate hot lists for full-submitted nodes;
- separate active/selected object lists;
- lazy adjacency generations;
- deferred garbage collection for explicitly removed objects.

Do not add generation machinery until benchmarks show these passes matter after
virtual submission is reduced.

## Phase 12 — Visible-path LOD and draw overhead

**Priority: workload-dependent.**

Virtual nodes only help off-screen content. Very low zoom can still put hundreds or
thousands of nodes on screen simultaneously.

Potential work:

- explicit low-zoom node LOD contract;
- simplified pin rendering at very small scales;
- suppress text/widgets below readability thresholds;
- batch or simplify link flow/highlight effects;
- cache text measurement/layout where application content is stable;
- reduce draw-channel work for simple visible nodes;
- avoid rebuilding static decorations when only runtime overlays change.

Keep this mostly application-controlled: the node editor does not know which custom
widgets are semantically safe to omit.

## Phase 13 — Render caching, only if still needed

**Priority: optional / late.**

After CPU virtualization, spatial indexing, and LOD are measured, consider GPU/render
caching for static content:

- static canvas layer cache;
- tile-based render textures;
- cached node shells/backgrounds;
- dirty-region redraw.

This is intentionally late because render textures add invalidation, zoom/DPI,
texture memory, and compositing complexity. They do not solve the original cost of
submitting thousands of off-screen immediate-mode nodes as directly as virtual
submission does.

A tile cache is preferable to one texture per node if this phase becomes necessary.

## Phase 14 — Group virtualization

**Priority: optional.**

Native `Group()` nodes remain on the full path because their bounds, resizing,
selection containment, and group hints have additional semantics.

Virtualize groups only after tests cover:

- group resize handles;
- group movement with members;
- group hints;
- selection rectangles;
- nested/overlapping behavior used by consumers.

Do not weaken the normal group path just to make the API symmetrical.

## Upstream/rebase strategy

Keep large-graph work isolated from compatibility work where possible.

Recommended commit categories:

```text
fix: Dear ImGui compatibility

test: compatibility/regression coverage

feat: virtual node API

perf: interaction/link/index optimization

bench: large graph benchmark
```

When rebasing onto upstream:

1. first make the upstream revision pass the compatibility suite with no large-graph
   changes;
2. replay virtual-node functionality;
3. replay performance-only changes;
4. run benchmark comparisons before/after each performance group.

Avoid depending on private Dear ImGui internals beyond what the existing editor
already requires unless there is a measured reason.

## Consumer rollout order

For TRAFFIQ or another existing editor:

1. integrate the fork and binding changes;
2. keep virtualization disabled by default;
3. reuse existing node/pin layout caches;
4. add Full / Editorless / Virtual routing;
5. compare behavior and frame profiles on representative graphs;
6. enable virtualization by default once interaction/save/restore regressions are
   ruled out;
7. collect benchmark data before starting Phase 9+.

## Stop conditions

Do not implement every phase automatically. Stop when representative workloads meet
the product's frame budget with acceptable worst-case interaction latency.

In particular, skip or postpone a phase when:

- its target cost is below measurement noise;
- it increases invalidation/lifetime complexity more than it saves;
- it mainly optimizes graph sizes the product does not need;
- a consumer-side LOD/cache solves the same measured bottleneck more safely.

The roadmap is therefore ordered by evidence: **integrate, benchmark, then deepen
retention only where the profile still demands it.**
