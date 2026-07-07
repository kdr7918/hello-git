# HLDB PointList Decode 이후 Qt Polygon 좌표 변환

## 전제

이 문서는 **PointList decode가 이미 끝난 뒤**를 기준으로 한다.

즉, 아래 단계는 이미 완료됐다고 가정한다.

```cpp
// 이미 완료된 상태라고 가정한다. 이 문서의 범위 아님.
std::vector<Point> decoded_points;
layout.decode_point_list(shape.point_list, shape.x, shape.y, decoded_points);
```

`decoded_points`는 HLDB/OASIS DBU 좌표계의 절대 좌표다.

- `Polygon`: decode된 점들을 그대로 닫힌 polygon으로 사용
- `Path`: decode된 점들은 **centerline**이므로 `half_width`만큼 outline polygon으로 확장 필요
- `Trapezoid`: PointList가 없고 `x/y/width/height/delta_a/delta_b/vertical`에서 4개 꼭짓점 생성
- Qt 렌더링: 최종적으로 `QPolygonF`를 만들어 `QPainter::drawPolygon()`으로 그림

## 좌표계 변환

HLDB/OASIS layout 좌표는 보통 수학 좌표계처럼 `y`가 위로 증가한다고 보고, Qt widget 좌표는 `y`가 아래로 증가한다.

따라서 world/layout DBU 좌표 `(x, y)`를 Qt pixel 좌표로 바꿀 때는 `y`를 뒤집는다.

```cpp
struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

struct BBox {
    std::int64_t min_x = 0;
    std::int64_t min_y = 0;
    std::int64_t max_x = 0;
    std::int64_t max_y = 0;
};

struct ViewportTransform {
    BBox world;       // 현재 화면에 보이는 layout DBU 영역
    double pixel_w;   // Qt widget width
    double pixel_h;   // Qt widget height

    double worldWidth() const {
        return std::max<double>(1.0, static_cast<double>(world.max_x - world.min_x));
    }

    double worldHeight() const {
        return std::max<double>(1.0, static_cast<double>(world.max_y - world.min_y));
    }

    QPointF toQt(Point p) const {
        const double sx = (static_cast<double>(p.x - world.min_x) / worldWidth()) * pixel_w;

        // Qt는 y-down, layout은 y-up으로 취급하므로 max_y 기준으로 뒤집는다.
        const double sy = (static_cast<double>(world.max_y - p.y) / worldHeight()) * pixel_h;

        return QPointF(sx, sy);
    }
};
```

공통 변환 함수는 다음처럼 둔다.

```cpp
static QPolygonF toQtPolygon(const std::vector<Point>& world_points,
                             const ViewportTransform& view) {
    QPolygonF polygon;
    polygon.reserve(static_cast<int>(world_points.size()));

    for (const Point& p : world_points) {
        polygon << view.toQt(p);
    }
    return polygon;
}
```

렌더링은 다음처럼 한다.

```cpp
QPainter painter(widget);
painter.setPen(QPen(color, 1.0));
painter.setBrush(QBrush(fill_color));
painter.drawPolygon(qpolygon);
```

## PolygonShape: decoded PointList → QPolygonF

`PolygonShape`는 PointList decode 이후 이미 polygon 꼭짓점 배열이다.

따라서 추가 geometry 변환 없이 Qt 좌표계로만 바꾼다.

```cpp
static QPolygonF polygonShapeToQtPolygon(const std::vector<Point>& decoded_points,
                                         const ViewportTransform& view) {
    if (decoded_points.size() < 3) {
        return {};
    }
    return toQtPolygon(decoded_points, view);
}
```

사용 예:

```cpp
std::vector<Point> decoded_points = /* decode_point_list 이후 결과 */;
QPolygonF qpoly = polygonShapeToQtPolygon(decoded_points, view);
if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## PathShape: decoded centerline → outline polygon → QPolygonF

`PathShape`의 decoded PointList는 polygon이 아니라 **중심선(centerline)** 이다.

Qt `drawPolyline()`으로 그리면 실제 OASIS path 폭이 반영되지 않는다. HLDB viewer 방식처럼 중심선을 `half_width`만큼 양쪽으로 offset해서 닫힌 outline polygon을 만든 뒤 `drawPolygon()`으로 그린다.

### Path shape 입력 구조

```cpp
struct PathShapeForRender {
    std::uint64_t half_width = 0;
    std::int64_t start_extension = 0;
    std::int64_t end_extension = 0;
};
```

### Path outline 변환 코드

```cpp
struct FloatPoint {
    long double x = 0.0L;
    long double y = 0.0L;
};

static FloatPoint operator+(FloatPoint a, FloatPoint b) {
    return {a.x + b.x, a.y + b.y};
}

static FloatPoint operator-(FloatPoint a, FloatPoint b) {
    return {a.x - b.x, a.y - b.y};
}

static FloatPoint operator*(FloatPoint p, long double s) {
    return {p.x * s, p.y * s};
}

static long double length(FloatPoint p) {
    return std::sqrt(p.x * p.x + p.y * p.y);
}

static std::optional<FloatPoint> normalized(FloatPoint p) {
    const long double len = length(p);
    if (len <= 1e-12L) return std::nullopt;
    return FloatPoint{p.x / len, p.y / len};
}

static long double cross(FloatPoint a, FloatPoint b) {
    return a.x * b.y - a.y * b.x;
}

static std::optional<FloatPoint> lineIntersection(FloatPoint origin_a,
                                                  FloatPoint direction_a,
                                                  FloatPoint origin_b,
                                                  FloatPoint direction_b) {
    const long double denom = cross(direction_a, direction_b);
    if (std::abs(denom) <= 1e-12L) return std::nullopt;

    const long double t = cross(origin_b - origin_a, direction_b) / denom;
    return origin_a + direction_a * t;
}

static std::int64_t roundToI64(long double v) {
    if (!std::isfinite(v)) return 0;

    const long double lo = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const long double hi = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (v <= lo) return std::numeric_limits<std::int64_t>::min();
    if (v >= hi) return std::numeric_limits<std::int64_t>::max();

    return static_cast<std::int64_t>(std::llround(v));
}

static void appendUnique(std::vector<Point>& points, Point p) {
    if (!points.empty() && points.back().x == p.x && points.back().y == p.y) return;
    points.push_back(p);
}

static std::vector<Point> pathCenterlineToOutlinePolygon(
    const PathShapeForRender& path,
    const std::vector<Point>& decoded_centerline) {

    if (decoded_centerline.size() < 2 || path.half_width == 0) {
        return {};
    }

    // 1. consecutive duplicate center points 제거
    std::vector<FloatPoint> centers;
    centers.reserve(decoded_centerline.size());

    for (const Point& p : decoded_centerline) {
        FloatPoint fp{static_cast<long double>(p.x), static_cast<long double>(p.y)};
        if (centers.empty() || length(fp - centers.back()) > 1e-12L) {
            centers.push_back(fp);
        }
    }

    if (centers.size() < 2) {
        return {};
    }

    // 2. start/end extension 적용
    const auto first_dir = normalized(centers[1] - centers[0]);
    const auto last_dir = normalized(centers[centers.size() - 1] - centers[centers.size() - 2]);
    if (!first_dir || !last_dir) {
        return {};
    }

    centers.front() = centers.front() - (*first_dir * static_cast<long double>(path.start_extension));
    centers.back() = centers.back() + (*last_dir * static_cast<long double>(path.end_extension));

    // 3. segment direction과 left normal 계산
    std::vector<FloatPoint> directions;
    std::vector<FloatPoint> normals;
    directions.reserve(centers.size() - 1);
    normals.reserve(centers.size() - 1);

    for (std::size_t i = 1; i < centers.size(); ++i) {
        auto dir = normalized(centers[i] - centers[i - 1]);
        if (!dir) return {};

        directions.push_back(*dir);
        normals.push_back(FloatPoint{-dir->y, dir->x});
    }

    const long double hw = static_cast<long double>(std::min<std::uint64_t>(
        path.half_width,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));

    // 너무 sharp한 corner에서 miter가 과도하게 튀는 것을 막는다.
    const long double miter_limit = std::max(hw * 8.0L, hw + 1.0L);

    auto offsetVertex = [&](std::size_t index, long double side) -> FloatPoint {
        if (index == 0) {
            return centers[index] + normals.front() * hw * side;
        }

        if (index + 1 == centers.size()) {
            return centers[index] + normals.back() * hw * side;
        }

        const FloatPoint prev_origin = centers[index] + normals[index - 1] * hw * side;
        const FloatPoint next_origin = centers[index] + normals[index] * hw * side;

        const auto miter = lineIntersection(prev_origin, directions[index - 1],
                                            next_origin, directions[index]);
        if (miter && length(*miter - centers[index]) <= miter_limit) {
            return *miter;
        }

        // fallback: adjacent normal을 섞은 bevel-like point
        const auto blended = normalized(normals[index - 1] + normals[index]);
        if (blended) {
            return centers[index] + (*blended * hw * side);
        }

        return centers[index] + normals[index] * hw * side;
    };

    // 4. 좌측 rail forward + 우측 rail reverse로 닫힌 outline 구성
    std::vector<Point> outline;
    outline.reserve(centers.size() * 2);

    for (std::size_t i = 0; i < centers.size(); ++i) {
        const FloatPoint p = offsetVertex(i, +1.0L);
        appendUnique(outline, Point{roundToI64(p.x), roundToI64(p.y)});
    }

    for (std::size_t i = centers.size(); i > 0; --i) {
        const FloatPoint p = offsetVertex(i - 1, -1.0L);
        appendUnique(outline, Point{roundToI64(p.x), roundToI64(p.y)});
    }

    if (outline.size() >= 2 &&
        outline.front().x == outline.back().x &&
        outline.front().y == outline.back().y) {
        outline.pop_back();
    }

    return outline.size() >= 3 ? outline : std::vector<Point>{};
}
```

### Path → QPolygonF

```cpp
static QPolygonF pathShapeToQtPolygon(const PathShapeForRender& path,
                                      const std::vector<Point>& decoded_centerline,
                                      const ViewportTransform& view) {
    const std::vector<Point> outline = pathCenterlineToOutlinePolygon(path, decoded_centerline);
    if (outline.empty()) {
        return {};
    }
    return toQtPolygon(outline, view);
}
```

사용 예:

```cpp
std::vector<Point> decoded_centerline = /* decode_point_list 이후 결과 */;

PathShapeForRender path;
path.half_width = shape.half_width;
path.start_extension = shape.start_extension;
path.end_extension = shape.end_extension;

QPolygonF qpoly = pathShapeToQtPolygon(path, decoded_centerline, view);
if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## TrapezoidShape: shape field → 4-point polygon → QPolygonF

`TrapezoidShape`는 PointList decode 대상이 아니다. 저장된 box와 delta 정보만으로 4개 꼭짓점을 만든다.

```cpp
struct TrapezoidShapeForRender {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::int64_t delta_a = 0;
    std::int64_t delta_b = 0;
    std::uint8_t vertical = 0;
};
```

### Trapezoid → world polygon points

```cpp
static std::int64_t u64ToI64Clamped(std::uint64_t v) {
    return static_cast<std::int64_t>(std::min<std::uint64_t>(
        v,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
}

static std::vector<Point> trapezoidToPolygonPoints(const TrapezoidShapeForRender& t) {
    const std::int64_t w = u64ToI64Clamped(t.width);
    const std::int64_t h = u64ToI64Clamped(t.height);

    const std::int64_t x0 = t.x;
    const std::int64_t y0 = t.y;
    const std::int64_t x1 = x0 + w;
    const std::int64_t y1 = y0 + h;

    if (t.vertical != 0) {
        // vertical trapezoid: 왼쪽 edge의 y 좌표에 delta 적용
        return {
            {x0, y0 + t.delta_a},
            {x0, y1 + t.delta_b},
            {x1, y1},
            {x1, y0},
        };
    }

    // horizontal trapezoid: 위쪽 edge의 x 좌표에 delta 적용
    return {
        {x0, y0},
        {x1, y0},
        {x1 + t.delta_b, y1},
        {x0 + t.delta_a, y1},
    };
}
```

### Trapezoid → QPolygonF

```cpp
static QPolygonF trapezoidShapeToQtPolygon(const TrapezoidShapeForRender& t,
                                           const ViewportTransform& view) {
    return toQtPolygon(trapezoidToPolygonPoints(t), view);
}
```

사용 예:

```cpp
TrapezoidShapeForRender t;
t.x = shape.x;
t.y = shape.y;
t.width = shape.width;
t.height = shape.height;
t.delta_a = shape.delta_a;
t.delta_b = shape.delta_b;
t.vertical = shape.vertical;

QPolygonF qpoly = trapezoidShapeToQtPolygon(t, view);
if (!qpoly.isEmpty()) {
    painter.drawPolygon(qpoly);
}
```

## 실제 QPainter 렌더링 흐름

```cpp
void drawPolygonShape(QPainter& painter,
                      const std::vector<Point>& decoded_polygon_points,
                      const ViewportTransform& view) {
    const QPolygonF qpoly = polygonShapeToQtPolygon(decoded_polygon_points, view);
    if (!qpoly.isEmpty()) painter.drawPolygon(qpoly);
}

void drawPathShape(QPainter& painter,
                   const PathShapeForRender& path,
                   const std::vector<Point>& decoded_centerline,
                   const ViewportTransform& view) {
    const QPolygonF qpoly = pathShapeToQtPolygon(path, decoded_centerline, view);
    if (!qpoly.isEmpty()) painter.drawPolygon(qpoly);
}

void drawTrapezoidShape(QPainter& painter,
                        const TrapezoidShapeForRender& trapezoid,
                        const ViewportTransform& view) {
    const QPolygonF qpoly = trapezoidShapeToQtPolygon(trapezoid, view);
    if (!qpoly.isEmpty()) painter.drawPolygon(qpoly);
}
```

## 정리

- PointList decode 이후 `Polygon`은 곧바로 `QPolygonF`로 변환한다.
- PointList decode 이후 `Path`는 decode 결과가 centerline이므로, 반드시 `half_width` 기반 outline polygon을 만든 뒤 `QPolygonF`로 변환한다.
- `Trapezoid`는 PointList가 없으므로 `x/y/width/height/delta_a/delta_b/vertical`에서 4개 꼭짓점을 만든 뒤 `QPolygonF`로 변환한다.
- HLDB/OASIS world 좌표에서 Qt widget 좌표로 갈 때는 viewport 기준 scale을 적용하고 `y`축을 뒤집는다.
- Qt에서는 최종적으로 세 타입 모두 `QPainter::drawPolygon(QPolygonF)`로 통일해서 그릴 수 있다.
