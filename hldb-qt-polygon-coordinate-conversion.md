# HLDB PointList Decode 이후 Qt Polygon 좌표 변환 (C++11)

## 전제

이 문서는 **PointList decode가 이미 끝난 뒤**를 기준으로 한다.  
즉, 아래 작업은 이미 완료되어 있고, 이 문서의 범위가 아니다.

```cpp
std::vector<Point> decoded_points;
layout.decode_point_list(shape.point_list, shape.x, shape.y, decoded_points);
```

`decoded_points`는 HLDB/OASIS DBU 좌표계의 절대 좌표라고 가정한다.

- `Polygon`: decode된 점 배열을 그대로 Qt `QPolygonF`로 변환
- `Path`: decode된 점 배열은 polygon이 아니라 **centerline**이므로 `half_width`만큼 outline polygon 생성 후 Qt `QPolygonF`로 변환
- `Trapezoid`: PointList가 없으므로 `x/y/width/height/delta_a/delta_b/vertical`에서 4개 꼭짓점 생성 후 Qt `QPolygonF`로 변환
- 최종 렌더링은 세 타입 모두 `QPainter::drawPolygon(QPolygonF)`로 통일 가능

아래 코드는 **C++11 문법** 기준이다.

## 필요한 include

```cpp
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QPainter>
#include <QPointF>
#include <QPolygonF>
```

## 기본 타입

```cpp
struct Point {
    int64_t x;
    int64_t y;

    Point() : x(0), y(0) {}
    Point(int64_t x_value, int64_t y_value) : x(x_value), y(y_value) {}
};

static bool samePoint(const Point& a, const Point& b) {
    return a.x == b.x && a.y == b.y;
}

struct BBox {
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;

    BBox() : min_x(0), min_y(0), max_x(0), max_y(0) {}
    BBox(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
        : min_x(x0), min_y(y0), max_x(x1), max_y(y1) {}
};
```

## HLDB/OASIS world 좌표 → Qt widget 좌표

HLDB/OASIS layout 좌표는 보통 수학 좌표계처럼 `y`가 위로 증가한다고 보고, Qt widget 좌표는 `y`가 아래로 증가한다.

그래서 `x`는 viewport left 기준으로 scale하고, `y`는 viewport `max_y` 기준으로 뒤집는다.

```cpp
struct ViewportTransform {
    BBox world;
    double pixel_w;
    double pixel_h;

    ViewportTransform() : world(), pixel_w(1.0), pixel_h(1.0) {}
    ViewportTransform(const BBox& world_box, double width_px, double height_px)
        : world(world_box), pixel_w(width_px), pixel_h(height_px) {}

    double worldWidth() const {
        const double w = static_cast<double>(world.max_x - world.min_x);
        return std::max(1.0, w);
    }

    double worldHeight() const {
        const double h = static_cast<double>(world.max_y - world.min_y);
        return std::max(1.0, h);
    }

    QPointF toQt(const Point& p) const {
        const double sx =
            (static_cast<double>(p.x - world.min_x) / worldWidth()) * pixel_w;

        // Qt: y-down, layout/world: y-up
        const double sy =
            (static_cast<double>(world.max_y - p.y) / worldHeight()) * pixel_h;

        return QPointF(sx, sy);
    }
};
```

공통 변환 함수:

```cpp
static QPolygonF toQtPolygon(const std::vector<Point>& world_points,
                             const ViewportTransform& view) {
    QPolygonF polygon;
    polygon.reserve(static_cast<int>(world_points.size()));

    for (std::size_t i = 0; i < world_points.size(); ++i) {
        polygon << view.toQt(world_points[i]);
    }
    return polygon;
}
```

## 1. Polygon: decoded PointList → QPolygonF

PointList decode 이후 `Polygon`은 이미 polygon 꼭짓점 배열이다.  
따라서 geometry 변환 없이 Qt 좌표계로만 바꾼다.

```cpp
static QPolygonF polygonShapeToQtPolygon(const std::vector<Point>& decoded_points,
                                         const ViewportTransform& view) {
    if (decoded_points.size() < 3) {
        return QPolygonF();
    }
    return toQtPolygon(decoded_points, view);
}
```

사용 예:

```cpp
std::vector<Point> decoded_points;  // decode_point_list 이후 결과
QPolygonF qpoly = polygonShapeToQtPolygon(decoded_points, view);

if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## 2. Path: decoded centerline → outline polygon → QPolygonF

`Path`의 PointList decode 결과는 polygon 외곽선이 아니라 중심선(centerline)이다.  
Qt `drawPolyline()`으로 그리면 OASIS path 폭이 반영되지 않는다.

따라서 다음 순서로 변환한다.

1. decoded centerline에서 연속 중복점 제거
2. 첫/마지막 segment 방향으로 `start_extension`, `end_extension` 적용
3. 각 segment 방향 벡터와 normal 계산
4. `half_width`만큼 양쪽 offset rail 생성
5. 내부 vertex는 인접 offset line 교차점으로 miter join
6. miter가 너무 길면 blended normal fallback
7. 닫힌 outline polygon을 만든 뒤 `QPolygonF`로 변환

### Path 입력 구조

```cpp
struct PathShapeForRender {
    uint64_t half_width;
    int64_t start_extension;
    int64_t end_extension;

    PathShapeForRender()
        : half_width(0), start_extension(0), end_extension(0) {}

    PathShapeForRender(uint64_t hw, int64_t start_ext, int64_t end_ext)
        : half_width(hw), start_extension(start_ext), end_extension(end_ext) {}
};
```

### Path outline 변환 코드

```cpp
struct FloatPoint {
    long double x;
    long double y;

    FloatPoint() : x(0.0L), y(0.0L) {}
    FloatPoint(long double x_value, long double y_value) : x(x_value), y(y_value) {}
};

static FloatPoint addPoint(const FloatPoint& a, const FloatPoint& b) {
    return FloatPoint(a.x + b.x, a.y + b.y);
}

static FloatPoint subPoint(const FloatPoint& a, const FloatPoint& b) {
    return FloatPoint(a.x - b.x, a.y - b.y);
}

static FloatPoint mulPoint(const FloatPoint& p, long double s) {
    return FloatPoint(p.x * s, p.y * s);
}

static long double lengthPoint(const FloatPoint& p) {
    return std::sqrt(p.x * p.x + p.y * p.y);
}

static bool normalizedPoint(const FloatPoint& p, FloatPoint* out) {
    const long double len = lengthPoint(p);
    if (len <= 1e-12L) {
        return false;
    }
    *out = FloatPoint(p.x / len, p.y / len);
    return true;
}

static long double crossPoint(const FloatPoint& a, const FloatPoint& b) {
    return a.x * b.y - a.y * b.x;
}

static bool lineIntersection(const FloatPoint& origin_a,
                             const FloatPoint& direction_a,
                             const FloatPoint& origin_b,
                             const FloatPoint& direction_b,
                             FloatPoint* out) {
    const long double denom = crossPoint(direction_a, direction_b);
    if (std::abs(denom) <= 1e-12L) {
        return false;
    }

    const long double t =
        crossPoint(subPoint(origin_b, origin_a), direction_b) / denom;
    *out = addPoint(origin_a, mulPoint(direction_a, t));
    return true;
}

static int64_t roundToI64(long double v) {
    if (!std::isfinite(v)) {
        return 0;
    }

    const long double lo =
        static_cast<long double>(std::numeric_limits<int64_t>::min());
    const long double hi =
        static_cast<long double>(std::numeric_limits<int64_t>::max());

    if (v <= lo) return std::numeric_limits<int64_t>::min();
    if (v >= hi) return std::numeric_limits<int64_t>::max();

    return static_cast<int64_t>(std::llround(v));
}

static void appendUnique(std::vector<Point>* points, const Point& p) {
    if (!points->empty() && samePoint(points->back(), p)) {
        return;
    }
    points->push_back(p);
}

static bool offsetVertex(const std::vector<FloatPoint>& centers,
                         const std::vector<FloatPoint>& directions,
                         const std::vector<FloatPoint>& normals,
                         std::size_t index,
                         long double side,
                         long double half_width,
                         long double miter_limit,
                         FloatPoint* out) {
    if (index == 0) {
        *out = addPoint(centers[index], mulPoint(normals.front(), half_width * side));
        return true;
    }

    if (index + 1 == centers.size()) {
        *out = addPoint(centers[index], mulPoint(normals.back(), half_width * side));
        return true;
    }

    const FloatPoint prev_origin =
        addPoint(centers[index], mulPoint(normals[index - 1], half_width * side));
    const FloatPoint next_origin =
        addPoint(centers[index], mulPoint(normals[index], half_width * side));

    FloatPoint miter;
    if (lineIntersection(prev_origin, directions[index - 1],
                         next_origin, directions[index], &miter)) {
        if (lengthPoint(subPoint(miter, centers[index])) <= miter_limit) {
            *out = miter;
            return true;
        }
    }

    FloatPoint blended = addPoint(normals[index - 1], normals[index]);
    FloatPoint blended_normal;
    if (normalizedPoint(blended, &blended_normal)) {
        *out = addPoint(centers[index], mulPoint(blended_normal, half_width * side));
        return true;
    }

    *out = addPoint(centers[index], mulPoint(normals[index], half_width * side));
    return true;
}

static std::vector<Point> pathCenterlineToOutlinePolygon(
    const PathShapeForRender& path,
    const std::vector<Point>& decoded_centerline) {

    if (decoded_centerline.size() < 2 || path.half_width == 0) {
        return std::vector<Point>();
    }

    // 1. consecutive duplicate center points 제거
    std::vector<FloatPoint> centers;
    centers.reserve(decoded_centerline.size());

    for (std::size_t i = 0; i < decoded_centerline.size(); ++i) {
        const Point& p = decoded_centerline[i];
        FloatPoint fp(static_cast<long double>(p.x), static_cast<long double>(p.y));

        if (centers.empty() || lengthPoint(subPoint(fp, centers.back())) > 1e-12L) {
            centers.push_back(fp);
        }
    }

    if (centers.size() < 2) {
        return std::vector<Point>();
    }

    // 2. start/end extension 적용
    FloatPoint first_dir;
    FloatPoint last_dir;

    if (!normalizedPoint(subPoint(centers[1], centers[0]), &first_dir)) {
        return std::vector<Point>();
    }

    if (!normalizedPoint(subPoint(centers[centers.size() - 1],
                                  centers[centers.size() - 2]), &last_dir)) {
        return std::vector<Point>();
    }

    centers.front() = subPoint(
        centers.front(),
        mulPoint(first_dir, static_cast<long double>(path.start_extension)));

    centers.back() = addPoint(
        centers.back(),
        mulPoint(last_dir, static_cast<long double>(path.end_extension)));

    // 3. segment direction과 left normal 계산
    std::vector<FloatPoint> directions;
    std::vector<FloatPoint> normals;
    directions.reserve(centers.size() - 1);
    normals.reserve(centers.size() - 1);

    for (std::size_t i = 1; i < centers.size(); ++i) {
        FloatPoint dir;
        if (!normalizedPoint(subPoint(centers[i], centers[i - 1]), &dir)) {
            return std::vector<Point>();
        }

        directions.push_back(dir);
        normals.push_back(FloatPoint(-dir.y, dir.x));
    }

    const uint64_t max_i64_u64 =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    const long double hw = static_cast<long double>(
        std::min<uint64_t>(path.half_width, max_i64_u64));

    // 너무 sharp한 corner에서 miter가 과도하게 튀는 것을 막는다.
    const long double miter_limit = std::max(hw * 8.0L, hw + 1.0L);

    // 4. 좌측 rail forward + 우측 rail reverse로 닫힌 outline 구성
    std::vector<Point> outline;
    outline.reserve(centers.size() * 2);

    for (std::size_t i = 0; i < centers.size(); ++i) {
        FloatPoint p;
        if (!offsetVertex(centers, directions, normals, i, +1.0L,
                          hw, miter_limit, &p)) {
            return std::vector<Point>();
        }
        appendUnique(&outline, Point(roundToI64(p.x), roundToI64(p.y)));
    }

    for (std::size_t i = centers.size(); i > 0; --i) {
        FloatPoint p;
        if (!offsetVertex(centers, directions, normals, i - 1, -1.0L,
                          hw, miter_limit, &p)) {
            return std::vector<Point>();
        }
        appendUnique(&outline, Point(roundToI64(p.x), roundToI64(p.y)));
    }

    if (outline.size() >= 2 && samePoint(outline.front(), outline.back())) {
        outline.pop_back();
    }

    if (outline.size() < 3) {
        return std::vector<Point>();
    }
    return outline;
}
```

### Path → QPolygonF

```cpp
static QPolygonF pathShapeToQtPolygon(const PathShapeForRender& path,
                                      const std::vector<Point>& decoded_centerline,
                                      const ViewportTransform& view) {
    const std::vector<Point> outline =
        pathCenterlineToOutlinePolygon(path, decoded_centerline);

    if (outline.empty()) {
        return QPolygonF();
    }
    return toQtPolygon(outline, view);
}
```

사용 예:

```cpp
std::vector<Point> decoded_centerline;  // decode_point_list 이후 결과

PathShapeForRender path(shape.half_width,
                        shape.start_extension,
                        shape.end_extension);

QPolygonF qpoly = pathShapeToQtPolygon(path, decoded_centerline, view);
if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## 3. Trapezoid: shape field → 4-point polygon → QPolygonF

`Trapezoid`는 PointList decode 대상이 아니다.  
저장된 box와 delta 정보만으로 4개 꼭짓점을 만든다.

```cpp
struct TrapezoidShapeForRender {
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    int64_t delta_a;
    int64_t delta_b;
    uint8_t vertical;

    TrapezoidShapeForRender()
        : x(0), y(0), width(0), height(0),
          delta_a(0), delta_b(0), vertical(0) {}

    TrapezoidShapeForRender(int64_t x_value,
                            int64_t y_value,
                            uint64_t width_value,
                            uint64_t height_value,
                            int64_t delta_a_value,
                            int64_t delta_b_value,
                            uint8_t vertical_value)
        : x(x_value), y(y_value), width(width_value), height(height_value),
          delta_a(delta_a_value), delta_b(delta_b_value),
          vertical(vertical_value) {}
};
```

### Trapezoid → world polygon points

```cpp
static int64_t u64ToI64Clamped(uint64_t v) {
    const uint64_t max_i64_u64 =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    return static_cast<int64_t>(std::min<uint64_t>(v, max_i64_u64));
}

static std::vector<Point> trapezoidToPolygonPoints(
    const TrapezoidShapeForRender& t) {

    const int64_t w = u64ToI64Clamped(t.width);
    const int64_t h = u64ToI64Clamped(t.height);

    const int64_t x0 = t.x;
    const int64_t y0 = t.y;
    const int64_t x1 = x0 + w;
    const int64_t y1 = y0 + h;

    std::vector<Point> points;
    points.reserve(4);

    if (t.vertical != 0) {
        // vertical trapezoid: 왼쪽 edge의 y 좌표에 delta 적용
        points.push_back(Point(x0, y0 + t.delta_a));
        points.push_back(Point(x0, y1 + t.delta_b));
        points.push_back(Point(x1, y1));
        points.push_back(Point(x1, y0));
        return points;
    }

    // horizontal trapezoid: 위쪽 edge의 x 좌표에 delta 적용
    points.push_back(Point(x0, y0));
    points.push_back(Point(x1, y0));
    points.push_back(Point(x1 + t.delta_b, y1));
    points.push_back(Point(x0 + t.delta_a, y1));
    return points;
}
```

### Trapezoid → QPolygonF

```cpp
static QPolygonF trapezoidShapeToQtPolygon(const TrapezoidShapeForRender& t,
                                           const ViewportTransform& view) {
    const std::vector<Point> points = trapezoidToPolygonPoints(t);
    return toQtPolygon(points, view);
}
```

사용 예:

```cpp
TrapezoidShapeForRender t(shape.x,
                          shape.y,
                          shape.width,
                          shape.height,
                          shape.delta_a,
                          shape.delta_b,
                          shape.vertical);

QPolygonF qpoly = trapezoidShapeToQtPolygon(t, view);
if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## 4. 실제 QPainter 렌더링 흐름

```cpp
static void drawPolygonShape(QPainter& painter,
                             const std::vector<Point>& decoded_polygon_points,
                             const ViewportTransform& view) {
    const QPolygonF qpoly = polygonShapeToQtPolygon(decoded_polygon_points, view);
    if (!qpoly.isEmpty()) {
        painter.drawPolygon(qpoly);
    }
}

static void drawPathShape(QPainter& painter,
                          const PathShapeForRender& path,
                          const std::vector<Point>& decoded_centerline,
                          const ViewportTransform& view) {
    const QPolygonF qpoly = pathShapeToQtPolygon(path, decoded_centerline, view);
    if (!qpoly.isEmpty()) {
        painter.drawPolygon(qpoly);
    }
}

static void drawTrapezoidShape(QPainter& painter,
                               const TrapezoidShapeForRender& trapezoid,
                               const ViewportTransform& view) {
    const QPolygonF qpoly = trapezoidShapeToQtPolygon(trapezoid, view);
    if (!qpoly.isEmpty()) {
        painter.drawPolygon(qpoly);
    }
}
```

## 정리

- `Polygon`: PointList decode 결과가 곧 polygon vertex 배열이므로 `QPolygonF`로 바로 변환한다.
- `Path`: PointList decode 결과가 centerline이므로 `half_width`, `start_extension`, `end_extension`을 반영해 닫힌 outline polygon을 만든 뒤 `QPolygonF`로 변환한다.
- `Trapezoid`: PointList가 없으므로 field 값에서 4개 꼭짓점을 만든 뒤 `QPolygonF`로 변환한다.
- HLDB/OASIS world 좌표에서 Qt widget 좌표로 갈 때는 viewport scale을 적용하고 `y`축을 뒤집는다.
- 위 코드는 C++11 기준으로 작성했으므로 C++17 `std::optional`, C++20 designated initializer, defaulted comparison 등을 쓰지 않는다.
