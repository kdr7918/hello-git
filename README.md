# hello-git
깃허브 테스트

github test 
branch is good
HAHAHA

## Boost.Polygon 1.63 custom point ops

`boost_polygon_custom_point_ops.cpp` contains a C++11 / Boost.Polygon 1.63 example that:

- uses a custom `Point` object through Boost.Polygon point traits
- classifies polygons as `Polygon90`, `Polygon45`, or `AnyAngle`
- runs Boolean `OR`, `AND`, `SUB`, `XOR`
- runs size up/down through Boost.Polygon 1.63 `resize()` APIs
- returns fractured no-hole polygons only

Build:

```sh
c++ -std=c++11 boost_polygon_custom_point_ops.cpp -o boost_polygon_custom_point_ops
```

If Boost 1.63 is not in the compiler default include path:

```sh
c++ -std=c++11 -I/path/to/boost_1_63_0 boost_polygon_custom_point_ops.cpp -o boost_polygon_custom_point_ops
```

## PointArray -> Boost.Polygon polygon_90_set benchmark

`boost_polygon90_pointarray_benchmark.cpp` is a self-contained C++11 example for
legacy-style geometry containers:

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
    // copy/move/ownership helpers included in the example
};
```

The public boolean shape is:

```cpp
std::vector<PointArray> output = booleanRectFastPath(lhs, rhs, BooleanOp::Or);
```

The implementation targets rectilinear rectangle/polygon data and uses
`boost::polygon::polygon_90_set_data<int64_t>`.  The benchmark intentionally
measures **PointArray -> Boost conversion + boolean + Boost -> PointArray
extraction**, because conversion cost is material for this API shape.

Variants included:

- `generic tmp vector each polygon`: converts every `PointArray` through a fresh
  temporary point vector.
- `generic reusable scratch`: reuses a scratch point vector while converting
  polygons.
- `rect fast path + scratch`: detects 4-point rectangles and inserts
  `rectangle_data` directly; falls back to polygon conversion for non-rectangles.
- `prebuilt sets boolean+extract`: measures boolean + extraction after both
  Boost sets are already built. This is useful for separating pure boolean time,
  but it does **not** include input conversion.

Build:

```sh
c++ -O3 -DNDEBUG -std=c++11 boost_polygon90_pointarray_benchmark.cpp -o boost_polygon90_pointarray_benchmark
```

Run:

```sh
./boost_polygon90_pointarray_benchmark 20000 3
```

Latest local benchmark on this machine, generated rectilinear data, 20,000 LHS +
20,000 RHS polygons, best-of-3:

- All rectangles, OR:
  - generic tmp vector each polygon: 27.362 ms
  - generic reusable scratch: 24.380 ms
  - **rect fast path + scratch: 22.995 ms** ← fastest full PointArray pipeline
  - prebuilt sets boolean+extract: 19.011 ms, excludes input conversion
- 80% rectangles + 20% stair polygons, OR:
  - generic tmp vector each polygon: 30.050 ms
  - generic reusable scratch: 28.575 ms
  - **rect fast path + scratch: 26.909 ms** ← fastest full PointArray pipeline
  - prebuilt sets boolean+extract: 20.641 ms, excludes input conversion
- All rectangles, AND:
  - generic tmp vector each polygon: 18.115 ms
  - generic reusable scratch: 16.260 ms
  - **rect fast path + scratch: 14.993 ms** ← fastest full PointArray pipeline
  - prebuilt sets boolean+extract: 11.692 ms, excludes input conversion

Conclusion: for `std::vector<PointArray>` input/output, the fastest measured
full-pipeline implementation is `booleanRectFastPath`: rectangle detection +
`rectangle_data` insertion + reusable scratch conversion for non-rectilinear
polygons.  If data is already cached as `polygon_90_set_data`, pure
boolean+extract is faster, but that excludes the required conversion cost.
