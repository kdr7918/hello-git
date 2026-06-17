# PointArray Boost.Polygon Boolean Dispatcher

C++11 example for boolean operations on legacy-style geometry containers:

```cpp
struct Point {
    int64_t x;
    int64_t y;
};

class PointArray {
public:
    uint32_t size;
    uint32_t numPoints;
    Point* points;
};
```

Input/output shape:

```cpp
std::vector<PointArray> lhs;
std::vector<PointArray> rhs;
BooleanResult result = booleanAuto(lhs, rhs, BooleanOp::Or);
std::vector<PointArray> output = result.polygons;
```

## Core idea

Geometry is classified before conversion so the fastest safe Boost.Polygon engine
is selected:

1. **All 90-degree rectilinear edges**
   - Use `boost::polygon::polygon_90_set_data<int64_t>`.
   - 4-point rectangles use a fast path through
     `boost::polygon::rectangle_data<int64_t>`.
   - Non-rectangle rectilinear polygons reuse one scratch vector and convert to
     `polygon_90_data<int64_t>`.

2. **Only 0/45/90-degree edges**
   - Use `boost::polygon::polygon_45_set_data<int64_t>`.
   - This avoids falling all the way back to the generic arbitrary-angle engine
     when 45-degree geometry is present.

3. **Any other edge angle**
   - Use `boost::polygon::polygon_set_data<int64_t>`.
   - This is the safe fallback for arbitrary-angle polygons.

The benchmark reports separate timing columns for:

- `in_conv`: geometry classification + `PointArray -> Boost.Polygon` insertion
- `boolean`: Boost.Polygon boolean operation
- `get`: `Boost.Polygon set.get(...)` extraction into Boost polygon objects
- `out_conv`: Boost polygon object -> `PointArray` conversion

## Copy-friendly engine dispatch snippet

```cpp
enum class GeometryKind { Polygon90, Polygon45, AnyAngle };

static GeometryKind edgeKind(const Point& a, const Point& b) {
    const int64_t dx = b.x - a.x;
    const int64_t dy = b.y - a.y;
    if (dx == 0 || dy == 0) return GeometryKind::Polygon90;

    const int64_t adx = dx < 0 ? -dx : dx;
    const int64_t ady = dy < 0 ? -dy : dy;
    if (adx == ady) return GeometryKind::Polygon45;

    return GeometryKind::AnyAngle;
}

static GeometryKind classifyOne(const PointArray& pa) {
    GeometryKind out = GeometryKind::Polygon90;
    for (uint32_t i = 0; i < pa.numPoints; ++i) {
        GeometryKind e = edgeKind(pa.points[i], pa.points[(i + 1) % pa.numPoints]);
        if (e == GeometryKind::AnyAngle) return GeometryKind::AnyAngle;
        if (e == GeometryKind::Polygon45) out = GeometryKind::Polygon45;
    }
    return out;
}
```

Full buildable code is in:

```text
pointarray_boost_boolean_benchmark.cpp
```

## Build

```sh
c++ -O3 -DNDEBUG -std=c++11 pointarray_boost_boolean_benchmark.cpp -o pointarray_boost_boolean_benchmark
```

## Run

```sh
# Default: 1,000,000 LHS + 1,000,000 RHS polygons per dataset, one round.
./pointarray_boost_boolean_benchmark

# Override count and rounds.
./pointarray_boost_boolean_benchmark 1000000 1
```

## Benchmark datasets

The benchmark creates three datasets:

- `90 rectilinear`
  - rectangles plus complex rectilinear comb polygons
  - expected engine: `polygon_90_set_data`
- `45 mixed`
  - rectangles plus 45-degree diamonds
  - expected engine: `polygon_45_set_data`
- `any angle mixed`
  - 45-degree diamonds plus arbitrary-angle triangles
  - expected engine: `polygon_set_data`

## Notes

- The 90-degree path is still fastest for purely rectilinear data because it can
  use both `polygon_90_set_data` and the rectangle fast path.
- The 45-degree path prevents unnecessary fallback to arbitrary-angle boolean for
  layouts that contain Manhattan + diagonal-45 edges only.
- The any-angle path is intentionally the fallback because it is more general and
  usually slower.

## Latest local 1M benchmark

Command:

```sh
./pointarray_boost_boolean_benchmark 1000000 1
```

Result on this machine, OR operation, best-of-1:

```text
dataset             selected engine               total    in_conv    boolean        get   out_conv   out polys  out points
90 rectilinear      polygon_90_set_data        2562.114    294.938    988.739   1172.494    105.944     1000000    10800000
45 mixed            polygon_45_set_data        3468.651    507.334    231.696   2641.705     87.916     1000000     7000000
any angle mixed     polygon_set_data          20719.792    548.009   9859.926  10215.691     96.166     1000000     8500000
```

Timing columns are milliseconds:

- `total`: `in_conv + boolean + get + out_conv`
- `in_conv`: geometry classification + input `PointArray` insertion into Boost.Polygon set
- `boolean`: Boost.Polygon boolean operation only
- `get`: `set.get(...)` into Boost polygon objects
- `out_conv`: Boost polygon objects converted back to `PointArray`

Conclusion: keep the dispatch order as `90 -> 45 -> any-angle`.  The 90-degree
engine is fastest when legal, the 45-degree engine is the right middle path for
Manhattan + diagonal-45 data, and the generic any-angle engine should be used
only when at least one arbitrary-angle edge exists.
