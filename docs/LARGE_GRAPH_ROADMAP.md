# Large-Graph Performance

This document records the large-graph performance work that is already implemented
in this fork and the optimizations that were deliberately deferred.

The goal is to keep large graphs responsive without turning `imgui-node-editor`
into a different retained GUI framework or adding complexity that is not justified
by measured costs.

## Current implementation

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

## Completed performance work

The following optimizations are implemented and should no longer be treated as
roadmap TODOs.

### Remove redundant pin/link sorting

Pin and link lookup now uses the existing lookup maps. `CreatePin()` and
`CreateLink()` therefore no longer sort the full retained vectors after every
insertion.

This removes an old requirement from the previous `lower_bound()` lookup path and
substantially reduces initial construction cost for large graphs.

### Remove adjacency duplicate scans

Per-frame adjacency is rebuilt from an empty generation. Registration therefore no
longer performs a linear `std::find()` before every append.

Same-LinkId resubmission still unregisters the previous adjacency first. Self-links
are explicitly prevented from inserting the same pin/node adjacency twice.

This avoids quadratic behavior for high-degree nodes such as star topologies.

### Avoid persistence work when persistence is disabled

When no settings file and no save callback are configured, the editor no longer
serializes settings every frame while leaving the persistent dirty state set.

A real save failure is still treated differently: dirty state is retained so the
save can be retried.

### Cache node settings serialization

The settings serializer retains the existing JSON tree and updates changed node
entries instead of reconstructing every node object for every save.

The external settings format is unchanged. Final JSON output still requires a full
`dump()`, so the last output step remains O(number of serialized settings).

### Skip unchanged link geometry updates

Pins carry a geometry revision. Links remember the revisions of their start/end
pins and skip endpoint/Bezier/bounds recomputation when the relevant geometry has
not changed.

Node movement and pin geometry changes advance the revision, so connected links are
still refreshed when required.

### Generation-based object lifetime and lazy frame reset

Frame start no longer eagerly resets every retained node, pin, link, and adjacency
bucket.

Object liveness is tracked with frame generations. Per-frame node/pin state is reset
lazily when the object is actually submitted, and adjacency buckets use generations
instead of a global clear pass.

Deletion cleanup is only scanned when deletion has actually been requested.

This removes the previous O(total retained objects) `Begin()` reset from normal
frames.

### Avoid redundant active-node reordering

Active-node lookup uses the retained order index rather than scanning the node
vector. If the active node is already at the front of its Z-order tier, the editor
also skips the redundant rotate/sort path.

## Performance characteristics

The important scaling properties after the completed work are:

- retained object lookup is map-based rather than dependent on sorted vectors;
- adjacency construction is linear in submitted links rather than quadratic in node
  degree;
- normal `Begin()` work is no longer proportional to every retained object;
- unchanged links reuse endpoint/Bezier/bounds geometry;
- off-screen nodes can use the virtual-node path and avoid normal ImGui node
  contents/draw channels;
- interaction and visible-link work use retained spatial broad-phase indexes.

Development profiling during this optimization work used graphs up to roughly
100k nodes / 100k links. Those measurements were useful for finding bottlenecks but
are not a committed benchmark contract; application-level profiling should remain
the deciding signal for further work.

## Remaining known costs

These are known costs, not automatic implementation tasks.

### Spatial-index rebuilds

The spatial indexes are retained, but a relevant bounds change can still cause a
later query to rebuild the affected index globally. This is O(total indexed
objects).

At 10k-scale workloads this was not large enough to justify the bookkeeping and
invalidation complexity of fully incremental membership updates. Revisit only if
profiling shows spatial rebuilds dominating node drag, topology edits, or other
representative workloads.

### Final settings JSON output

Cached node entries avoid rebuilding unchanged node objects, but final JSON `dump()`
still walks the complete serialized document.

This can produce save-time spikes for very large settings sets. It does not affect
normal frames when no save is required.

### Link submission

Links are still submitted each frame. Cached geometry makes unchanged submissions
cheaper, but the submission loop remains O(total submitted links).

A persistent link/topology API could remove this cost, but that would change the
lifetime contract substantially and is intentionally not part of the current
optimization set.

### Visible draw cost

Virtualization removes off-screen node rendering cost. It cannot remove the cost of
thousands of nodes that are simultaneously visible at low zoom.

Visible ImGui geometry, text, link tessellation, draw channels, and canvas vertex
transforms therefore remain workload-dependent costs.

## Deferred optimizations

The following work was investigated and intentionally left out because it requires a
larger architectural or visible-behavior change than the measured benefit currently
justifies.

### Incremental spatial-index updates

Possible approach:

```text
object
  -> previous occupied cells
  -> remove old memberships
  -> insert new memberships
```

A complete implementation must handle:

- previous-cell ownership/back-references;
- removal without stale pointers;
- negative cell coordinates;
- large-object overflow membership;
- deletion and generation lifetime interaction;
- group/resize and multi-node movement;
- links that change endpoint pins under the same LinkId;
- efficient bulk restore/import.

Do not implement a partial incremental path that leaves ambiguous ownership between
full rebuilds and per-object updates. Add it only if profiling demonstrates that
spatial rebuilds are a real bottleneck.

### Long-link spatial indexing

Links are currently indexed by retained bounds. A very long diagonal Bezier can
cover a large AABB and may use the spatial overflow path even though the curve itself
occupies little area.

A possible future solution is to index a link as a small number of curve segments
instead of one large AABB. That requires decisions about segment count/flatness,
query-result deduplication, update cost, and interaction with link geometry cache.

This is topology-dependent and should only be implemented for workloads that
actually contain enough long cross-graph links to make overflow scanning expensive.

### Persistent virtual objects / topology

Virtual nodes are lightweight but are still submitted each frame. A retained API
could conceptually provide operations such as:

```cpp
RegisterVirtualNode(...);
UpdateVirtualNode(...);
RemoveVirtualNode(...);
```

with the semantic distinction:

```text
not submitted this frame != dead
```

The current generation-based lifetime optimization does not provide this API; it
only removes unnecessary internal reset work while preserving the immediate-mode
submission contract.

A persistent topology API would require explicit lifetime, topology invalidation,
restore behavior, full-vs-virtual override rules, and stale pin/link handling. It is
deferred until per-frame virtual submission itself becomes a measured bottleneck.

### Visible-path LOD

Possible low-zoom work includes:

- simplified pin rendering;
- suppressing unreadable text/widgets;
- reducing link tessellation/detail;
- simplifying node shells/backgrounds;
- reducing runtime effects such as flow/highlight rendering.

This changes visible behavior and should be exposed as an explicit application/editor
feature rather than introduced as an invisible internal optimization. The editor
also cannot know which custom widgets are semantically safe to omit.

### Canvas/GPU transform or render caching

Canvas local-space vertices are transformed on the CPU. GPU-side transforms or
render caches could reduce CPU work for very large visible scenes, but they affect
renderer/backend architecture and introduce additional invalidation, zoom/DPI,
texture-memory, and compositing concerns.

If render caching is eventually required, a tile/static-layer approach is preferable
to one texture per node.

### Incremental settings output

The final serialized settings string could theoretically be maintained incrementally
instead of calling a complete JSON `dump()`.

That would complicate ordering, escaping, deletion, compatibility, and save-failure
semantics for a cost that occurs only when settings are written. Keep the current
cached-tree implementation unless real application save profiles justify a more
complex writer.

### Group virtualization

Native `Group()` nodes remain on the full path because their bounds, resizing,
selection containment, and group hints have additional semantics.

Virtualize groups only if required and only after coverage exists for:

- group resize handles;
- group movement with members;
- group hints;
- selection rectangles;
- nested/overlapping behavior used by consumers.

## Benchmarking future changes

Do not add another optimization because it is theoretically faster. Measure the
specific path first.

Useful graph sizes include:

```text
1,000
10,000
100,000 nodes
```

with local links, high-degree/star links, and long cross-graph links tested
separately when relevant.

Useful timing regions include:

- editor `Begin()`;
- full and virtual node submission;
- pin submission;
- interaction broad phase;
- spatial queries/rebuilds;
- link submission/geometry update;
- visible-link rebuild;
- editor `End()`;
- settings save/serialization when testing persistence.

Representative scenarios should include idle, pan, zoom, one-node drag, multi-node
drag, link hover, rectangle selection, and topology edits at several visible ratios.

## Upstream/rebase strategy

Keep large-graph work isolated from compatibility work where possible.

When rebasing onto upstream:

1. make the upstream revision pass the compatibility suite before replaying
   large-graph changes;
2. replay virtual-node functionality;
3. replay performance-only changes in small commits;
4. compare representative profiles after each performance group;
5. do not import an upstream fix that reintroduces full-graph scans without checking
   it against the retained/spatial architecture in this fork.

Avoid depending on additional private Dear ImGui internals unless there is a measured
reason.

## Stop conditions

The current performance pass should be considered complete unless representative
consumer workloads still show `imgui-node-editor` itself as the dominant frame cost.

Reopen deferred work only when:

- a profiler identifies the corresponding path as material;
- the target graph size is relevant to the consumer;
- the improvement is large enough to justify the additional lifetime/invalidation
  complexity; and
- the change can be covered by regression and performance tests.

Do not optimize deferred items merely to make the roadmap complete.
