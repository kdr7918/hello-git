// boost_polygon_custom_point_ops.cpp
// C++11 + Boost.Polygon
//
// Features:
// - Uses a user-defined Point object as input/output.
// - Classifies polygon edges as 90-degree, 45-degree, or any-angle.
// - Chooses Boost.Polygon's polygon_90_set_data, polygon_45_set_data, or
//   polygon_set_data based on that classification.
// - Supports Boolean OR, AND, SUB, XOR and size up/down via resize().
//
// Build:
//   c++ -std=c++11 boost_polygon_custom_point_ops.cpp -o boost_polygon_custom_point_ops

#include <boost/polygon/polygon.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace custom_poly {

typedef std::int64_t Coord;

struct Point {
    Coord x;
    Coord y;

    Point() : x(0), y(0) {}
    Point(Coord x_, Coord y_) : x(x_), y(y_) {}
};

typedef std::vector<Point> Ring;

struct Polygon {
    Ring outer;
    std::vector<Ring> holes;
};

typedef std::vector<Polygon> PolygonList;

enum class PolygonKind {
    Polygon90,
    Polygon45,
    AnyAngle
};

enum class BooleanOp {
    Or,
    And,
    Sub,
    Xor
};

struct OperationResult {
    PolygonKind engineKind;
    PolygonList polygons;
};

} // namespace custom_poly

namespace boost {
namespace polygon {

template <>
struct geometry_concept<custom_poly::Point> {
    typedef point_concept type;
};

template <>
struct point_traits<custom_poly::Point> {
    typedef custom_poly::Coord coordinate_type;

    static inline coordinate_type get(
        const custom_poly::Point& point,
        orientation_2d orient)
    {
        return orient == HORIZONTAL ? point.x : point.y;
    }
};

template <>
struct point_mutable_traits<custom_poly::Point> {
    typedef custom_poly::Coord coordinate_type;

    static inline void set(
        custom_poly::Point& point,
        orientation_2d orient,
        coordinate_type value)
    {
        if (orient == HORIZONTAL) {
            point.x = value;
        } else {
            point.y = value;
        }
    }

    static inline custom_poly::Point construct(
        coordinate_type xValue,
        coordinate_type yValue)
    {
        return custom_poly::Point(xValue, yValue);
    }
};

} // namespace polygon
} // namespace boost

namespace custom_poly {

namespace bp = boost::polygon;

typedef bp::polygon_90_with_holes_data<Coord> BoostPolygon90;
typedef bp::polygon_45_with_holes_data<Coord> BoostPolygon45;
typedef bp::polygon_with_holes_data<Coord> BoostPolygonAny;

typedef bp::polygon_90_set_data<Coord> BoostSet90;
typedef bp::polygon_45_set_data<Coord> BoostSet45;
typedef bp::polygon_set_data<Coord> BoostSetAny;

static bool samePoint(const Point& a, const Point& b)
{
    return a.x == b.x && a.y == b.y;
}

static Coord absCoord(Coord value)
{
    if (value == std::numeric_limits<Coord>::min()) {
        throw std::overflow_error("coordinate difference overflow");
    }

    return value < 0 ? -value : value;
}

static Ring makeOpenCleanRing(const Ring& ring)
{
    Ring out;
    out.reserve(ring.size());

    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (out.empty() || !samePoint(out.back(), ring[i])) {
            out.push_back(ring[i]);
        }
    }

    while (out.size() >= 2 && samePoint(out.front(), out.back())) {
        out.pop_back();
    }

    return out;
}

static void requireValidRing(const Ring& ring, const char* ringName)
{
    const Ring clean = makeOpenCleanRing(ring);

    if (clean.size() < 3) {
        throw std::invalid_argument(std::string(ringName) + " needs at least 3 vertices");
    }
}

static PolygonKind mergeKinds(PolygonKind a, PolygonKind b)
{
    if (a == PolygonKind::AnyAngle || b == PolygonKind::AnyAngle) {
        return PolygonKind::AnyAngle;
    }

    if (a == PolygonKind::Polygon45 || b == PolygonKind::Polygon45) {
        return PolygonKind::Polygon45;
    }

    return PolygonKind::Polygon90;
}

static PolygonKind classifyRing(const Ring& ring)
{
    const Ring clean = makeOpenCleanRing(ring);

    if (clean.size() < 3) {
        throw std::invalid_argument("ring needs at least 3 vertices");
    }

    bool all90 = true;
    bool all45 = true;

    for (std::size_t i = 0; i < clean.size(); ++i) {
        const Point& a = clean[i];
        const Point& b = clean[(i + 1) % clean.size()];
        const Coord dx = b.x - a.x;
        const Coord dy = b.y - a.y;

        const bool edge90 = (dx == 0 || dy == 0);
        const bool edge45 = edge90 || (absCoord(dx) == absCoord(dy));

        all90 = all90 && edge90;
        all45 = all45 && edge45;
    }

    if (all90) {
        return PolygonKind::Polygon90;
    }

    if (all45) {
        return PolygonKind::Polygon45;
    }

    return PolygonKind::AnyAngle;
}

static PolygonKind classifyPolygon(const Polygon& polygon)
{
    requireValidRing(polygon.outer, "outer ring");

    PolygonKind kind = classifyRing(polygon.outer);

    for (std::size_t i = 0; i < polygon.holes.size(); ++i) {
        requireValidRing(polygon.holes[i], "hole ring");
        kind = mergeKinds(kind, classifyRing(polygon.holes[i]));
    }

    return kind;
}

static PolygonKind classifyPolygonList(const PolygonList& polygons)
{
    PolygonKind kind = PolygonKind::Polygon90;

    for (std::size_t i = 0; i < polygons.size(); ++i) {
        kind = mergeKinds(kind, classifyPolygon(polygons[i]));
    }

    return kind;
}

static const char* toString(PolygonKind kind)
{
    switch (kind) {
    case PolygonKind::Polygon90:
        return "Polygon90";
    case PolygonKind::Polygon45:
        return "Polygon45";
    case PolygonKind::AnyAngle:
        return "AnyAngle";
    }

    return "Unknown";
}

template <typename BoostPolygon>
static BoostPolygon toBoostPolygon(const Polygon& polygon)
{
    typedef typename bp::polygon_with_holes_traits<BoostPolygon>::hole_type BoostHole;

    BoostPolygon out;

    const Ring outer = makeOpenCleanRing(polygon.outer);
    bp::set_points(out, outer.begin(), outer.end());

    std::vector<BoostHole> holes;
    holes.reserve(polygon.holes.size());

    for (std::size_t i = 0; i < polygon.holes.size(); ++i) {
        const Ring holeRing = makeOpenCleanRing(polygon.holes[i]);
        BoostHole hole;
        bp::set_points(hole, holeRing.begin(), holeRing.end());
        holes.push_back(hole);
    }

    if (!holes.empty()) {
        bp::set_holes(out, holes.begin(), holes.end());
    }

    return out;
}

template <typename BoostSet, typename BoostPolygon>
static BoostSet makeBoostSet(const PolygonList& polygons)
{
    BoostSet out;

    for (std::size_t i = 0; i < polygons.size(); ++i) {
        out.insert(toBoostPolygon<BoostPolygon>(polygons[i]));
    }

    return out;
}

template <typename BoostSet>
static BoostSet applyBooleanTyped(
    const BoostSet& lhs,
    const BoostSet& rhs,
    BooleanOp op)
{
    using namespace boost::polygon::operators;

    BoostSet out;

    switch (op) {
    case BooleanOp::Or:
        bp::assign(out, lhs | rhs);
        break;
    case BooleanOp::And:
        bp::assign(out, lhs & rhs);
        break;
    case BooleanOp::Sub:
        bp::assign(out, lhs - rhs);
        break;
    case BooleanOp::Xor:
        bp::assign(out, lhs ^ rhs);
        break;
    }

    return out;
}

static void resizeInPlace(
    BoostSet90& polygons,
    Coord delta,
    bool,
    unsigned int)
{
    bp::resize(polygons, delta);
}

static void resizeInPlace(
    BoostSet45& polygons,
    Coord delta,
    bool cornerFillArc,
    unsigned int circleSegments)
{
    bp::resize(polygons, delta, cornerFillArc, circleSegments);
}

static void resizeInPlace(
    BoostSetAny& polygons,
    Coord delta,
    bool cornerFillArc,
    unsigned int circleSegments)
{
    bp::resize(polygons, delta, cornerFillArc, circleSegments);
}

template <typename BoostSet>
static BoostSetAny toGenericSet(const BoostSet& polygons)
{
    BoostSetAny out;
    bp::assign(out, polygons);
    return out;
}

static Point fromBoostPoint(const bp::point_data<Coord>& point)
{
    return Point(
        bp::get(point, bp::HORIZONTAL),
        bp::get(point, bp::VERTICAL));
}

template <typename BoostPolygon>
static Ring ringFromBoostPolygon(const BoostPolygon& polygon)
{
    Ring out;

    for (typename bp::polygon_traits<BoostPolygon>::iterator_type it =
             bp::begin_points(polygon);
         it != bp::end_points(polygon);
         ++it) {
        out.push_back(fromBoostPoint(*it));
    }

    return out;
}

static PolygonList toCustomPolygons(const BoostSetAny& polygons)
{
    std::vector<BoostPolygonAny> boostPolygons;
    polygons.get(boostPolygons);

    PolygonList out;
    out.reserve(boostPolygons.size());

    for (std::size_t i = 0; i < boostPolygons.size(); ++i) {
        Polygon polygon;
        polygon.outer = ringFromBoostPolygon(boostPolygons[i]);

        for (bp::polygon_with_holes_traits<BoostPolygonAny>::iterator_holes_type it =
                 bp::begin_holes(boostPolygons[i]);
             it != bp::end_holes(boostPolygons[i]);
             ++it) {
            polygon.holes.push_back(ringFromBoostPolygon(*it));
        }

        out.push_back(polygon);
    }

    return out;
}

template <typename BoostSet>
static OperationResult makeResult(PolygonKind kind, const BoostSet& polygons)
{
    OperationResult result;
    result.engineKind = kind;
    result.polygons = toCustomPolygons(toGenericSet(polygons));
    return result;
}

OperationResult booleanPolygons(
    const PolygonList& lhs,
    const PolygonList& rhs,
    BooleanOp op)
{
    const PolygonKind kind = mergeKinds(
        classifyPolygonList(lhs),
        classifyPolygonList(rhs));

    if (kind == PolygonKind::Polygon90) {
        const BoostSet90 lhsSet = makeBoostSet<BoostSet90, BoostPolygon90>(lhs);
        const BoostSet90 rhsSet = makeBoostSet<BoostSet90, BoostPolygon90>(rhs);
        return makeResult(kind, applyBooleanTyped(lhsSet, rhsSet, op));
    }

    if (kind == PolygonKind::Polygon45) {
        const BoostSet45 lhsSet = makeBoostSet<BoostSet45, BoostPolygon45>(lhs);
        const BoostSet45 rhsSet = makeBoostSet<BoostSet45, BoostPolygon45>(rhs);
        return makeResult(kind, applyBooleanTyped(lhsSet, rhsSet, op));
    }

    const BoostSetAny lhsSet = makeBoostSet<BoostSetAny, BoostPolygonAny>(lhs);
    const BoostSetAny rhsSet = makeBoostSet<BoostSetAny, BoostPolygonAny>(rhs);
    return makeResult(kind, applyBooleanTyped(lhsSet, rhsSet, op));
}

OperationResult resizePolygons(
    const PolygonList& input,
    Coord delta,
    bool cornerFillArc = false,
    unsigned int circleSegments = 0)
{
    const PolygonKind kind = classifyPolygonList(input);

    if (kind == PolygonKind::Polygon90) {
        BoostSet90 set = makeBoostSet<BoostSet90, BoostPolygon90>(input);
        resizeInPlace(set, delta, cornerFillArc, circleSegments);
        return makeResult(kind, set);
    }

    if (kind == PolygonKind::Polygon45) {
        BoostSet45 set = makeBoostSet<BoostSet45, BoostPolygon45>(input);
        resizeInPlace(set, delta, cornerFillArc, circleSegments);
        return makeResult(kind, set);
    }

    BoostSetAny set = makeBoostSet<BoostSetAny, BoostPolygonAny>(input);
    resizeInPlace(set, delta, cornerFillArc, circleSegments);
    return makeResult(kind, set);
}

static Polygon makePolygon(const Ring& outer)
{
    Polygon polygon;
    polygon.outer = outer;
    return polygon;
}

static void printResultSummary(const char* label, const OperationResult& result)
{
    std::cout << label
              << " engine=" << toString(result.engineKind)
              << " polygons=" << result.polygons.size()
              << std::endl;
}

} // namespace custom_poly

int main()
{
    using custom_poly::BooleanOp;
    using custom_poly::Point;
    using custom_poly::PolygonList;
    using custom_poly::Ring;

    const Ring rectA = {
        Point(0, 0), Point(20, 0), Point(20, 20), Point(0, 20)
    };
    const Ring rectB = {
        Point(10, 10), Point(30, 10), Point(30, 30), Point(10, 30)
    };

    PolygonList a90;
    a90.push_back(custom_poly::makePolygon(rectA));

    PolygonList b90;
    b90.push_back(custom_poly::makePolygon(rectB));

    custom_poly::printResultSummary(
        "90 OR",
        custom_poly::booleanPolygons(a90, b90, BooleanOp::Or));

    custom_poly::printResultSummary(
        "90 AND",
        custom_poly::booleanPolygons(a90, b90, BooleanOp::And));

    custom_poly::printResultSummary(
        "90 SUB",
        custom_poly::booleanPolygons(a90, b90, BooleanOp::Sub));

    custom_poly::printResultSummary(
        "90 XOR",
        custom_poly::booleanPolygons(a90, b90, BooleanOp::Xor));

    custom_poly::printResultSummary(
        "90 SIZE UP",
        custom_poly::resizePolygons(a90, 5));

    custom_poly::printResultSummary(
        "90 SIZE DOWN",
        custom_poly::resizePolygons(a90, -5));

    const Ring diamond45 = {
        Point(10, 0), Point(20, 10), Point(10, 20), Point(0, 10)
    };

    PolygonList p45;
    p45.push_back(custom_poly::makePolygon(diamond45));

    custom_poly::printResultSummary(
        "45 SIZE UP",
        custom_poly::resizePolygons(p45, 3));

    const Ring anyAngleTriangle = {
        Point(0, 0), Point(25, 0), Point(11, 17)
    };

    PolygonList anyAngle;
    anyAngle.push_back(custom_poly::makePolygon(anyAngleTriangle));

    custom_poly::printResultSummary(
        "ANY SIZE UP",
        custom_poly::resizePolygons(anyAngle, 3, true, 16));

    return 0;
}
