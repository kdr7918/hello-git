// boost_polygon90_pointarray_benchmark.cpp
// C++11 + Boost.Polygon polygon_90_set_data benchmark for std::vector<PointArray>.
//
// The important API shape is intentionally close to legacy layout databases:
//   struct Point { int64_t x; int64_t y; };
//   class PointArray { uint32_t size; uint32_t numPoints; Point* points; };
//   std::vector<PointArray> input -> std::vector<PointArray> output.
//
// Benchmarked variants include conversion + boolean + extraction time, because
// type conversion often dominates small/medium rectilinear datasets.
//
// Build:
//   c++ -O3 -DNDEBUG -std=c++11 boost_polygon90_pointarray_benchmark.cpp -o boost_polygon90_pointarray_benchmark

#include <boost/polygon/polygon.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace pa90 {

namespace bp = boost::polygon;

typedef std::int64_t Coord;
typedef bp::point_data<Coord> BoostPoint;
typedef bp::rectangle_data<Coord> BoostRect;
typedef bp::polygon_90_data<Coord> BoostPolygon90;
typedef bp::polygon_90_with_holes_data<Coord> BoostPolygon90WithHoles;
typedef bp::polygon_data<Coord> BoostNoHolePolygon;
typedef bp::polygon_90_set_data<Coord> BoostPolygon90Set;

struct Point {
    Coord x;
    Coord y;

    Point() : x(0), y(0) {}
    Point(Coord x_, Coord y_) : x(x_), y(y_) {}
};

class PointArray {
public:
    std::uint32_t size;
    std::uint32_t numPoints;
    Point* points;

    PointArray() : size(0), numPoints(0), points(NULL) {}

    explicit PointArray(std::uint32_t n) : size(n), numPoints(n), points(NULL) {
        if (n != 0) {
            points = new Point[n];
        }
    }

    PointArray(const Point* src, std::uint32_t n) : size(n), numPoints(n), points(NULL) {
        if (n != 0) {
            points = new Point[n];
            std::copy(src, src + n, points);
        }
    }

    PointArray(const PointArray& other) : size(other.size), numPoints(other.numPoints), points(NULL) {
        if (other.numPoints != 0) {
            points = new Point[other.numPoints];
            std::copy(other.points, other.points + other.numPoints, points);
        }
    }

    PointArray(PointArray&& other) noexcept
        : size(other.size), numPoints(other.numPoints), points(other.points) {
        other.size = 0;
        other.numPoints = 0;
        other.points = NULL;
    }

    PointArray& operator=(const PointArray& other) {
        if (this == &other) {
            return *this;
        }
        Point* newPoints = NULL;
        if (other.numPoints != 0) {
            newPoints = new Point[other.numPoints];
            std::copy(other.points, other.points + other.numPoints, newPoints);
        }
        delete[] points;
        points = newPoints;
        size = other.size;
        numPoints = other.numPoints;
        return *this;
    }

    PointArray& operator=(PointArray&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        delete[] points;
        size = other.size;
        numPoints = other.numPoints;
        points = other.points;
        other.size = 0;
        other.numPoints = 0;
        other.points = NULL;
        return *this;
    }

    ~PointArray() {
        delete[] points;
    }

    Point& operator[](std::uint32_t i) { return points[i]; }
    const Point& operator[](std::uint32_t i) const { return points[i]; }
};

enum class BooleanOp {
    Or,
    And,
    Sub,
    Xor
};

static PointArray makePointArray(const std::vector<Point>& pts) {
    if (pts.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("too many points for PointArray::numPoints");
    }
    return PointArray(pts.empty() ? NULL : &pts[0], static_cast<std::uint32_t>(pts.size()));
}

static PointArray makeRect(Coord xl, Coord yl, Coord xh, Coord yh) {
    std::vector<Point> p;
    p.push_back(Point(xl, yl));
    p.push_back(Point(xh, yl));
    p.push_back(Point(xh, yh));
    p.push_back(Point(xl, yh));
    return makePointArray(p);
}

static PointArray makeStair(Coord x, Coord y, Coord step, int steps, Coord height) {
    // Rectilinear non-self-intersecting stair polygon, open ring.
    std::vector<Point> p;
    p.reserve(static_cast<std::size_t>(2 * steps + 4));
    p.push_back(Point(x, y));
    for (int i = 0; i < steps; ++i) {
        p.push_back(Point(x + step * (i + 1), y + step * i));
        p.push_back(Point(x + step * (i + 1), y + step * (i + 1)));
    }
    p.push_back(Point(x + step * steps, y + height));
    p.push_back(Point(x, y + height));
    return makePointArray(p);
}

static bool isRectilinear(const PointArray& pa) {
    if (pa.numPoints < 4) {
        return false;
    }
    for (std::uint32_t i = 0; i < pa.numPoints; ++i) {
        const Point& a = pa.points[i];
        const Point& b = pa.points[(i + 1) % pa.numPoints];
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
    }
    return true;
}

static bool tryGetRect(const PointArray& pa, BoostRect& rect) {
    if (pa.numPoints != 4) {
        return false;
    }

    Coord xl = pa.points[0].x;
    Coord xh = pa.points[0].x;
    Coord yl = pa.points[0].y;
    Coord yh = pa.points[0].y;
    for (std::uint32_t i = 1; i < 4; ++i) {
        xl = std::min(xl, pa.points[i].x);
        xh = std::max(xh, pa.points[i].x);
        yl = std::min(yl, pa.points[i].y);
        yh = std::max(yh, pa.points[i].y);
    }
    if (xl == xh || yl == yh) {
        return false;
    }

    bool hasLL = false, hasLR = false, hasUR = false, hasUL = false;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const Point& p = pa.points[i];
        hasLL = hasLL || (p.x == xl && p.y == yl);
        hasLR = hasLR || (p.x == xh && p.y == yl);
        hasUR = hasUR || (p.x == xh && p.y == yh);
        hasUL = hasUL || (p.x == xl && p.y == yh);
    }
    if (!(hasLL && hasLR && hasUR && hasUL)) {
        return false;
    }

    rect = BoostRect(xl, yl, xh, yh);
    return true;
}

static void pointArrayToPolygon(const PointArray& pa, std::vector<BoostPoint>& scratch, BoostPolygon90& poly) {
    if (!isRectilinear(pa)) {
        throw std::invalid_argument("PointArray contains a non-rectilinear edge");
    }
    scratch.clear();
    scratch.reserve(pa.numPoints);
    for (std::uint32_t i = 0; i < pa.numPoints; ++i) {
        scratch.push_back(BoostPoint(pa.points[i].x, pa.points[i].y));
    }
    bp::set_points(poly, scratch.begin(), scratch.end());
}

static void insertGenericPolygons(BoostPolygon90Set& set, const std::vector<PointArray>& input) {
    std::vector<BoostPoint> scratch;
    BoostPolygon90 poly;
    for (std::size_t i = 0; i < input.size(); ++i) {
        pointArrayToPolygon(input[i], scratch, poly);
        set.insert(poly);
    }
}

static void insertGenericNoScratchReuse(BoostPolygon90Set& set, const std::vector<PointArray>& input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::vector<BoostPoint> tmp;
        tmp.reserve(input[i].numPoints);
        BoostPolygon90 poly;
        pointArrayToPolygon(input[i], tmp, poly);
        set.insert(poly);
    }
}

static void insertRectFastPath(BoostPolygon90Set& set, const std::vector<PointArray>& input) {
    std::vector<BoostPoint> scratch;
    BoostPolygon90 poly;
    for (std::size_t i = 0; i < input.size(); ++i) {
        BoostRect rect;
        if (tryGetRect(input[i], rect)) {
            set.insert(rect);
        } else {
            pointArrayToPolygon(input[i], scratch, poly);
            set.insert(poly);
        }
    }
}

static BoostPolygon90Set applyBooleanCopy(const BoostPolygon90Set& lhs, const BoostPolygon90Set& rhs, BooleanOp op) {
    using namespace boost::polygon::operators;

    BoostPolygon90Set out;
    if (op == BooleanOp::Or) {
        bp::assign(out, lhs | rhs);
    } else if (op == BooleanOp::And) {
        bp::assign(out, lhs & rhs);
    } else if (op == BooleanOp::Sub) {
        bp::assign(out, lhs - rhs);
    } else {
        bp::assign(out, lhs ^ rhs);
    }
    return out;
}

static std::vector<PointArray> extractPointArrays(const BoostPolygon90Set& set) {
    // Getting polygon_data fractures holes into no-hole polygons, which maps
    // naturally to std::vector<PointArray> with one outer ring per object.
    std::vector<BoostNoHolePolygon> polys;
    set.get(polys);

    std::vector<PointArray> out;
    out.reserve(polys.size());
    for (std::size_t i = 0; i < polys.size(); ++i) {
        std::vector<Point> pts;
        for (bp::polygon_traits<BoostNoHolePolygon>::iterator_type it = bp::begin_points(polys[i]);
             it != bp::end_points(polys[i]);
             ++it) {
            pts.push_back(Point(bp::get(*it, bp::HORIZONTAL), bp::get(*it, bp::VERTICAL)));
        }
        if (!pts.empty()) {
            out.push_back(makePointArray(pts));
        }
    }
    return out;
}

static std::vector<PointArray> booleanGenericNoScratch(
    const std::vector<PointArray>& lhs,
    const std::vector<PointArray>& rhs,
    BooleanOp op)
{
    BoostPolygon90Set a;
    BoostPolygon90Set b;
    insertGenericNoScratchReuse(a, lhs);
    insertGenericNoScratchReuse(b, rhs);
    BoostPolygon90Set result = applyBooleanCopy(a, b, op);
    return extractPointArrays(result);
}

static std::vector<PointArray> booleanGenericScratch(
    const std::vector<PointArray>& lhs,
    const std::vector<PointArray>& rhs,
    BooleanOp op)
{
    BoostPolygon90Set a;
    BoostPolygon90Set b;
    insertGenericPolygons(a, lhs);
    insertGenericPolygons(b, rhs);
    BoostPolygon90Set result = applyBooleanCopy(a, b, op);
    return extractPointArrays(result);
}

static std::vector<PointArray> booleanRectFastPath(
    const std::vector<PointArray>& lhs,
    const std::vector<PointArray>& rhs,
    BooleanOp op)
{
    BoostPolygon90Set a;
    BoostPolygon90Set b;
    insertRectFastPath(a, lhs);
    insertRectFastPath(b, rhs);
    BoostPolygon90Set result = applyBooleanCopy(a, b, op);
    return extractPointArrays(result);
}

static std::vector<PointArray> booleanFromPrebuiltSets(
    const BoostPolygon90Set& lhs,
    const BoostPolygon90Set& rhs,
    BooleanOp op)
{
    BoostPolygon90Set result = applyBooleanCopy(lhs, rhs, op);
    return extractPointArrays(result);
}

struct Dataset {
    std::vector<PointArray> lhs;
    std::vector<PointArray> rhs;
};

static Dataset makeRectDataset(int count, Coord pitch, Coord size, Coord rhsShift) {
    Dataset d;
    d.lhs.reserve(count);
    d.rhs.reserve(count);
    for (int i = 0; i < count; ++i) {
        Coord x = static_cast<Coord>(i % 200) * pitch;
        Coord y = static_cast<Coord>(i / 200) * pitch;
        d.lhs.push_back(makeRect(x, y, x + size, y + size));
        d.rhs.push_back(makeRect(x + rhsShift, y + rhsShift, x + rhsShift + size, y + rhsShift + size));
    }
    return d;
}

static Dataset makeMixedDataset(int count, Coord pitch, Coord size, Coord rhsShift) {
    Dataset d;
    d.lhs.reserve(count);
    d.rhs.reserve(count);
    for (int i = 0; i < count; ++i) {
        Coord x = static_cast<Coord>(i % 200) * pitch;
        Coord y = static_cast<Coord>(i / 200) * pitch;
        if ((i % 5) == 0) {
            d.lhs.push_back(makeStair(x, y, size / 4, 4, size));
            d.rhs.push_back(makeStair(x + rhsShift, y + rhsShift, size / 4, 4, size));
        } else {
            d.lhs.push_back(makeRect(x, y, x + size, y + size));
            d.rhs.push_back(makeRect(x + rhsShift, y + rhsShift, x + rhsShift + size, y + rhsShift + size));
        }
    }
    return d;
}

struct Timing {
    std::string name;
    double totalMs;
    std::size_t outputPolygons;
    std::size_t outputPoints;
};

static std::size_t countPoints(const std::vector<PointArray>& polys) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < polys.size(); ++i) {
        n += polys[i].numPoints;
    }
    return n;
}

static double nowMs() {
    typedef std::chrono::high_resolution_clock Clock;
    static const Clock::time_point base = Clock::now();
    return std::chrono::duration<double, std::milli>(Clock::now() - base).count();
}

template <typename Func>
static Timing runTimed(const std::string& name, Func func, int rounds) {
    std::vector<PointArray> out;
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < rounds; ++i) {
        const double t0 = nowMs();
        out = func();
        const double t1 = nowMs();
        best = std::min(best, t1 - t0);
    }
    Timing timing;
    timing.name = name;
    timing.totalMs = best;
    timing.outputPolygons = out.size();
    timing.outputPoints = countPoints(out);
    return timing;
}

static void printTiming(const Timing& t) {
    std::cout << std::left << std::setw(34) << t.name
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) << t.totalMs
              << std::setw(14) << t.outputPolygons
              << std::setw(14) << t.outputPoints << '\n';
}

static const char* opName(BooleanOp op) {
    if (op == BooleanOp::Or) return "OR";
    if (op == BooleanOp::And) return "AND";
    if (op == BooleanOp::Sub) return "SUB";
    return "XOR";
}

static void benchmarkDataset(const std::string& label, const Dataset& d, BooleanOp op, int rounds) {
    std::cout << "\nDataset: " << label << ", lhs=" << d.lhs.size()
              << ", rhs=" << d.rhs.size() << ", op=" << opName(op)
              << ", best-of=" << rounds << '\n';
    std::cout << std::left << std::setw(34) << "variant"
              << std::right << std::setw(12) << "ms"
              << std::setw(14) << "out polys"
              << std::setw(14) << "out points" << '\n';

    std::vector<Timing> timings;
    timings.push_back(runTimed("generic tmp vector each polygon", [&]() {
        return booleanGenericNoScratch(d.lhs, d.rhs, op);
    }, rounds));
    timings.push_back(runTimed("generic reusable scratch", [&]() {
        return booleanGenericScratch(d.lhs, d.rhs, op);
    }, rounds));
    timings.push_back(runTimed("rect fast path + scratch", [&]() {
        return booleanRectFastPath(d.lhs, d.rhs, op);
    }, rounds));

    BoostPolygon90Set preA;
    BoostPolygon90Set preB;
    insertRectFastPath(preA, d.lhs);
    insertRectFastPath(preB, d.rhs);
    timings.push_back(runTimed("prebuilt sets boolean+extract", [&]() {
        return booleanFromPrebuiltSets(preA, preB, op);
    }, rounds));

    for (std::size_t i = 0; i < timings.size(); ++i) {
        printTiming(timings[i]);
    }

    std::vector<Timing>::const_iterator fastest = std::min_element(
        timings.begin(), timings.end(),
        [](const Timing& a, const Timing& b) { return a.totalMs < b.totalMs; });
    std::cout << "fastest: " << fastest->name << " (" << std::fixed << std::setprecision(3)
              << fastest->totalMs << " ms)" << '\n';
}

static void sanityCheck() {
    Dataset d = makeRectDataset(16, 20, 12, 6);
    std::vector<PointArray> a = booleanGenericScratch(d.lhs, d.rhs, BooleanOp::Or);
    std::vector<PointArray> b = booleanRectFastPath(d.lhs, d.rhs, BooleanOp::Or);
    if (a.size() != b.size() || countPoints(a) != countPoints(b)) {
        throw std::runtime_error("sanity check failed: variants disagree on output shape count");
    }
}

} // namespace pa90

int main(int argc, char** argv) {
    try {
        int count = 5000;
        int rounds = 5;
        if (argc > 1) {
            count = std::atoi(argv[1]);
        }
        if (argc > 2) {
            rounds = std::atoi(argv[2]);
        }
        if (count <= 0 || rounds <= 0) {
            throw std::invalid_argument("usage: ./boost_polygon90_pointarray_benchmark [polygon_count] [rounds]");
        }

        pa90::sanityCheck();
        const pa90::Dataset rects = pa90::makeRectDataset(count, 20, 12, 6);
        const pa90::Dataset mixed = pa90::makeMixedDataset(count, 20, 12, 6);

        std::cout << "Boost.Polygon polygon_90_set_data PointArray benchmark\n";
        std::cout << "Times include PointArray -> Boost conversion, boolean, and Boost -> PointArray extraction\n";
        std::cout << "except 'prebuilt sets boolean+extract', which excludes input conversion.\n";

        pa90::benchmarkDataset("all rectangles", rects, pa90::BooleanOp::Or, rounds);
        pa90::benchmarkDataset("80% rectangles + 20% stair polygons", mixed, pa90::BooleanOp::Or, rounds);
        pa90::benchmarkDataset("all rectangles", rects, pa90::BooleanOp::And, rounds);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
