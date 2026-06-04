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
