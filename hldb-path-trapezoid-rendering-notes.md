# HLDB Path/Trapezoid → Polygon Rendering Notes

## Scope

This note summarizes how the local HLDB viewer renders `Path` and `Trapezoid`
geometry by converting them to polygon point sequences at draw time. The source
inspection was done against:

- HLDB workspace: `/home/kdr0324/workspace/hldb`
- Main implementation: `cpp/src/viewer_session.cpp`
- Shape storage types: `cpp/include/hldb/layout/shapes.hpp`
- Point-list decoding and bbox support: `cpp/src/layout_model.cpp`
- Regression tests: `cpp/tests/viewer_session_test.cpp`

## Relevant stored shape types

`cpp/include/hldb/layout/shapes.hpp` stores geometry in compact layout-native
forms rather than normalizing everything to polygon upfront.

```cpp
struct PolygonShape {
  std::int64_t x = 0;
  std::int64_t y = 0;
  const std::uint8_t* point_list = nullptr;
};

struct PathShape {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::uint64_t half_width = 0;
  std::int64_t start_extension = 0;
  std::int64_t end_extension = 0;
  const std::uint8_t* point_list = nullptr;
};

struct TrapezoidShape {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::int64_t delta_a = 0;
  std::int64_t delta_b = 0;
  std::uint8_t vertical = 0;
  std::uint8_t reserved[7] = {};
};
```

So the viewer conversion is intentionally late-bound:

1. Decode points or derive corners in layout DBU coordinates.
2. For paths, expand the centerline into a closed outline polygon.
3. For trapezoids, derive the four polygon corners from width/height/deltas.
4. Apply placement/instance transform if rendering through hierarchy.
5. Convert world coordinates to SVG screen coordinates and emit `<polygon>`.

## Polygon point-list decoding

`Layout::decode_point_list()` is the shared point-list decoder used by both
`PolygonShape` and `PathShape`. It reads the compact encoded point-list payload,
starts from the shape origin `(x, y)`, applies decoded deltas cumulatively, and
returns absolute layout DBU points.

Important behavior from `cpp/src/layout_model.cpp`:

- point-list payload begins with `type` and `count`
- supported type range is `0..5`
- types `0/1` alternate horizontal/vertical signed deltas
- types `2/3` decode packed two-/three-delta forms
- type `4` decodes raw g-deltas
- type `5` decodes cumulative g-delta adjustments
- returned points include the origin as the first point

This means `PolygonShape` is already ready to draw after decoding, but
`PathShape` still needs width expansion.

## Path rendering conversion

Source: `cpp/src/viewer_session.cpp::path_outline_points()`.

Input:

- `PathShape path`
- decoded centerline points from `layout.decode_point_list(path.point_list,
  path.x, path.y, points)`

Algorithm:

1. Reject degenerate input if fewer than two center points or `half_width == 0`.
2. Remove consecutive duplicate center points.
3. Compute normalized first and last segment directions.
4. Apply path extensions:
   - `centers.front() -= first_direction * start_extension`
   - `centers.back()  += last_direction  * end_extension`
5. For every centerline segment, compute:
   - unit direction `d = normalize(next - current)`
   - left normal `n = (-d.y, d.x)`
6. Build two offset rails around the centerline:
   - forward side: `center + normal * half_width`
   - reverse side: `center - normal * half_width`
7. At interior vertices, intersect adjacent offset lines to create a miter join.
8. If the miter is too long, fall back to a blended-normal join.
9. Round coordinates back to `int64_t` DBU points.
10. Remove duplicated closing point if present.
11. Render the outline as a closed SVG `<polygon>`.

The current implementation uses:

```cpp
const long double miter_limit = std::max(half_width * 8.0L,
                                         half_width + 1.0L);
```

So very sharp turns do not generate unbounded spikes; they fall back to blended
normal points.

## Trapezoid rendering conversion

Source: `cpp/src/viewer_session.cpp::trapezoid_points()`.

Input:

- `TrapezoidShape{x, y, width, height, delta_a, delta_b, vertical}`

The converter clamps unsigned width/height to `int64_t` range, then derives the
box edges:

```cpp
x0 = trapezoid.x;
y0 = trapezoid.y;
x1 = x0 + width;
y1 = y0 + height;
```

For horizontal trapezoids (`vertical == 0`), deltas shift the top edge x
coordinates:

```cpp
(x0, y0)
(x1, y0)
(x1 + delta_b, y1)
(x0 + delta_a, y1)
```

For vertical trapezoids (`vertical != 0`), deltas shift the left edge y
coordinates:

```cpp
(x0, y0 + delta_a)
(x0, y1 + delta_b)
(x1, y1)
(x1, y0)
```

The derived four points are rendered as a closed SVG `<polygon>`.

## Render path in `viewer_session.cpp`

There are two relevant render functions:

- `draw_transformed_shape(...)`: used for hierarchy/placement-aware rendering.
  It decodes/derives local points, applies placement transform, and emits SVG.
- `draw_shape(...)`: used for direct shape rendering. It uses the same path
  outline conversion for `Path` and delegates trapezoid handling through
  `draw_transformed_shape(..., Transform{})`.

For transformed rendering:

```cpp
case ShapeKind::kPolygon:
case ShapeKind::kPath: {
  // decode point_list to absolute local DBU points
  // if Path: path_outline_points(path, center_points) -> closed polygon
  // else Polygon: draw decoded polygon as closed polygon
}
case ShapeKind::kTrapezoid: {
  // trapezoid_points(trapezoid) -> four polygon corners
}
```

Then `draw_transformed_points()` applies `apply_transform(transform, x, y)`, maps
world DBU to viewport pixels, and emits `<polygon points="...">` for closed
geometry.

## Fallback behavior

If conversion cannot be performed, the viewer does not crash. It falls back to
bbox drawing:

- missing shape pointer
- point-list decode failure
- empty decoded point-list
- path with invalid/degenerate outline

Fallback appears as a semi-transparent bbox. The tests verify that expected Path
and Trapezoid fixtures render as real polygons, not bbox fallback.

## Existing regression tests

`cpp/tests/viewer_session_test.cpp` includes:

- `trapezoids_render_as_polygons()`
  - opens `oasis/deep_hierarchy.oas`
  - renders layer `4:0`
  - asserts output contains `<polygon`
  - asserts bbox fallback opacity is absent
- `paths_render_as_filled_outlines()`
  - opens `oasis/geom_path.oas`
  - asserts output contains `<polygon`
  - asserts output does not contain `<polyline`

## Attached conversion code

A standalone C++ reference extraction is included in:

- [`hldb_path_trapezoid_polygon_conversion.cc`](hldb_path_trapezoid_polygon_conversion.cc)

It contains the Path centerline-to-outline conversion and Trapezoid-to-corner
conversion logic in a compact form suitable for review or reuse.
