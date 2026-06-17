// pointarray_boost_boolean_benchmark.cpp
// C++11 + Boost.Polygon boolean dispatcher for std::vector<PointArray>.
//
// It chooses the fastest safe Boost.Polygon engine for the input geometry:
//   - all orthogonal edges       -> polygon_90_set_data + rectangle fast path
//   - only 0/45/90 degree edges  -> polygon_45_set_data
//   - otherwise                  -> polygon_set_data
//
// Build:
//   c++ -O3 -DNDEBUG -std=c++11 pointarray_boost_boolean_benchmark.cpp -o pointarray_boost_boolean_benchmark
// Run:
//   ./pointarray_boost_boolean_benchmark 1000000 1

#include <boost/polygon/polygon.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pointarray_boolean {

namespace bp = boost::polygon;

typedef std::int64_t Coord;
typedef bp::point_data<Coord> BPoint;
typedef bp::rectangle_data<Coord> BRect;
typedef bp::polygon_90_data<Coord> BPoly90;
typedef bp::polygon_45_data<Coord> BPoly45;
typedef bp::polygon_data<Coord> BPolyAny;
typedef bp::polygon_data<Coord> BOutPoly;
typedef bp::polygon_90_set_data<Coord> BSet90;
typedef bp::polygon_45_set_data<Coord> BSet45;
typedef bp::polygon_set_data<Coord> BSetAny;

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
        if (n != 0) points = new Point[n];
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
        if (this == &other) return *this;
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
        if (this == &other) return *this;
        delete[] points;
        size = other.size;
        numPoints = other.numPoints;
        points = other.points;
        other.size = 0;
        other.numPoints = 0;
        other.points = NULL;
        return *this;
    }

    ~PointArray() { delete[] points; }
};

enum class BooleanOp { Or, And, Sub, Xor };
enum class GeometryKind { Polygon90, Polygon45, AnyAngle };

struct BooleanTiming {
    double inputConversionMs;
    double booleanMs;
    double getMs;
    double outputConversionMs;

    BooleanTiming()
        : inputConversionMs(0.0), booleanMs(0.0), getMs(0.0), outputConversionMs(0.0) {}

    double totalMs() const {
        return inputConversionMs + booleanMs + getMs + outputConversionMs;
    }
};

struct BooleanResult {
    GeometryKind engine;
    BooleanTiming timing;
    std::vector<PointArray> polygons;
};

static const char* kindName(GeometryKind k) {
    if (k == GeometryKind::Polygon90) return "polygon_90_set_data";
    if (k == GeometryKind::Polygon45) return "polygon_45_set_data";
    return "polygon_set_data";
}

static double nowMs();

static PointArray makePointArray(const std::vector<Point>& pts) {
    if (pts.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("too many points for PointArray");
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

static PointArray makeDiamond45(Coord cx, Coord cy, Coord r) {
    std::vector<Point> p;
    p.push_back(Point(cx, cy - r));
    p.push_back(Point(cx + r, cy));
    p.push_back(Point(cx, cy + r));
    p.push_back(Point(cx - r, cy));
    return makePointArray(p);
}

static PointArray makeAnyTriangle(Coord x, Coord y, Coord w, Coord h) {
    std::vector<Point> p;
    p.push_back(Point(x, y));
    p.push_back(Point(x + w, y + h / 3));
    p.push_back(Point(x + w / 5, y + h));
    return makePointArray(p);
}

static PointArray makeComb(Coord x, Coord y, Coord tooth, int teeth, Coord depth) {
    std::vector<Point> p;
    const Coord w = tooth * teeth;
    const Coord h = depth * 3;
    p.reserve(static_cast<std::size_t>(2 * teeth + 4));
    p.push_back(Point(x, y));
    p.push_back(Point(x + w, y));
    p.push_back(Point(x + w, y + h));
    for (int i = teeth - 1; i >= 0; --i) {
        const Coord right = x + tooth * (i + 1);
        const Coord left = x + tooth * i;
        if ((i % 2) == 0) {
            p.push_back(Point(right, y + h - depth));
            p.push_back(Point(left, y + h - depth));
        } else {
            p.push_back(Point(right, y + h));
            p.push_back(Point(left, y + h));
        }
    }
    return makePointArray(p);
}

static GeometryKind edgeKind(const Point& a, const Point& b) {
    const Coord dx = b.x - a.x;
    const Coord dy = b.y - a.y;
    if (dx == 0 || dy == 0) return GeometryKind::Polygon90;
    const Coord adx = dx < 0 ? -dx : dx;
    const Coord ady = dy < 0 ? -dy : dy;
    if (adx == ady) return GeometryKind::Polygon45;
    return GeometryKind::AnyAngle;
}

static GeometryKind classifyOne(const PointArray& pa) {
    if (pa.numPoints < 3) throw std::invalid_argument("polygon has fewer than 3 points");
    GeometryKind out = GeometryKind::Polygon90;
    for (std::uint32_t i = 0; i < pa.numPoints; ++i) {
        GeometryKind e = edgeKind(pa.points[i], pa.points[(i + 1) % pa.numPoints]);
        if (e == GeometryKind::AnyAngle) return GeometryKind::AnyAngle;
        if (e == GeometryKind::Polygon45) out = GeometryKind::Polygon45;
    }
    return out;
}

static GeometryKind classifyAll(const std::vector<PointArray>& a, const std::vector<PointArray>& b) {
    GeometryKind out = GeometryKind::Polygon90;
    for (std::size_t i = 0; i < a.size(); ++i) {
        GeometryKind k = classifyOne(a[i]);
        if (k == GeometryKind::AnyAngle) return GeometryKind::AnyAngle;
        if (k == GeometryKind::Polygon45) out = GeometryKind::Polygon45;
    }
    for (std::size_t i = 0; i < b.size(); ++i) {
        GeometryKind k = classifyOne(b[i]);
        if (k == GeometryKind::AnyAngle) return GeometryKind::AnyAngle;
        if (k == GeometryKind::Polygon45) out = GeometryKind::Polygon45;
    }
    return out;
}

static bool tryGetRect(const PointArray& pa, BRect& rect) {
    if (pa.numPoints != 4) return false;
    Coord xl = pa.points[0].x, xh = pa.points[0].x;
    Coord yl = pa.points[0].y, yh = pa.points[0].y;
    for (std::uint32_t i = 1; i < 4; ++i) {
        xl = std::min(xl, pa.points[i].x);
        xh = std::max(xh, pa.points[i].x);
        yl = std::min(yl, pa.points[i].y);
        yh = std::max(yh, pa.points[i].y);
    }
    if (xl == xh || yl == yh) return false;
    bool ll = false, lr = false, ur = false, ul = false;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const Point& p = pa.points[i];
        ll = ll || (p.x == xl && p.y == yl);
        lr = lr || (p.x == xh && p.y == yl);
        ur = ur || (p.x == xh && p.y == yh);
        ul = ul || (p.x == xl && p.y == yh);
    }
    if (!(ll && lr && ur && ul)) return false;
    rect = BRect(xl, yl, xh, yh);
    return true;
}

static void fillScratch(const PointArray& pa, std::vector<BPoint>& scratch) {
    scratch.clear();
    scratch.reserve(pa.numPoints);
    for (std::uint32_t i = 0; i < pa.numPoints; ++i) {
        scratch.push_back(BPoint(pa.points[i].x, pa.points[i].y));
    }
}

static void insert90(BSet90& set, const std::vector<PointArray>& input) {
    std::vector<BPoint> scratch;
    BPoly90 poly;
    for (std::size_t i = 0; i < input.size(); ++i) {
        BRect rect;
        if (tryGetRect(input[i], rect)) {
            set.insert(rect);
        } else {
            fillScratch(input[i], scratch);
            bp::set_points(poly, scratch.begin(), scratch.end());
            set.insert(poly);
        }
    }
}

static void insert45(BSet45& set, const std::vector<PointArray>& input) {
    std::vector<BPoint> scratch;
    BPoly45 poly;
    for (std::size_t i = 0; i < input.size(); ++i) {
        fillScratch(input[i], scratch);
        bp::set_points(poly, scratch.begin(), scratch.end());
        set.insert(poly);
    }
}

static void insertAny(BSetAny& set, const std::vector<PointArray>& input) {
    std::vector<BPoint> scratch;
    BPolyAny poly;
    for (std::size_t i = 0; i < input.size(); ++i) {
        fillScratch(input[i], scratch);
        bp::set_points(poly, scratch.begin(), scratch.end());
        set.insert(poly);
    }
}

template <typename Set>
static Set applyBoolean(const Set& lhs, const Set& rhs, BooleanOp op) {
    using namespace boost::polygon::operators;
    Set out;
    if (op == BooleanOp::Or) bp::assign(out, lhs | rhs);
    else if (op == BooleanOp::And) bp::assign(out, lhs & rhs);
    else if (op == BooleanOp::Sub) bp::assign(out, lhs - rhs);
    else bp::assign(out, lhs ^ rhs);
    return out;
}

template <typename Set, typename OutPoly>
static std::vector<OutPoly> getBoostPolygons(const Set& set) {
    std::vector<OutPoly> polys;
    set.get(polys);
    return polys;
}

static std::vector<BPoly90> getBoostPolygons90(const BSet90& set, std::size_t vertexThreshold) {
    std::vector<BPoly90> polys;
    if (vertexThreshold == 0) {
        set.get(polys);
    } else {
        set.get(polys, vertexThreshold);
    }
    return polys;
}

template <typename BoostPoly>
static std::vector<PointArray> convertBoostPolygonsToPointArrays(const std::vector<BoostPoly>& polys) {
    std::vector<PointArray> out;
    out.reserve(polys.size());
    for (std::size_t i = 0; i < polys.size(); ++i) {
        std::vector<Point> pts;
        for (typename bp::polygon_traits<BoostPoly>::iterator_type it = bp::begin_points(polys[i]);
             it != bp::end_points(polys[i]); ++it) {
            pts.push_back(Point(bp::get(*it, bp::HORIZONTAL), bp::get(*it, bp::VERTICAL)));
        }
        if (!pts.empty()) out.push_back(makePointArray(pts));
    }
    return out;
}

static BooleanResult booleanAuto(const std::vector<PointArray>& lhs,
                                const std::vector<PointArray>& rhs,
                                BooleanOp op,
                                std::size_t getVertexThreshold = 0) {
    BooleanResult result;

    double t0 = nowMs();
    result.engine = classifyAll(lhs, rhs);

    if (result.engine == GeometryKind::Polygon90) {
        BSet90 a, b;
        insert90(a, lhs);
        insert90(b, rhs);
        double t1 = nowMs();

        BSet90 out = applyBoolean(a, b, op);
        double t2 = nowMs();

        std::vector<BPoly90> boostPolys = getBoostPolygons90(out, getVertexThreshold);
        double t3 = nowMs();

        result.polygons = convertBoostPolygonsToPointArrays(boostPolys);
        double t4 = nowMs();

        result.timing.inputConversionMs = t1 - t0;
        result.timing.booleanMs = t2 - t1;
        result.timing.getMs = t3 - t2;
        result.timing.outputConversionMs = t4 - t3;
    } else if (result.engine == GeometryKind::Polygon45) {
        BSet45 a, b;
        insert45(a, lhs);
        insert45(b, rhs);
        double t1 = nowMs();

        BSet45 out = applyBoolean(a, b, op);
        double t2 = nowMs();

        std::vector<BOutPoly> boostPolys = getBoostPolygons<BSet45, BOutPoly>(out);
        double t3 = nowMs();

        result.polygons = convertBoostPolygonsToPointArrays(boostPolys);
        double t4 = nowMs();

        result.timing.inputConversionMs = t1 - t0;
        result.timing.booleanMs = t2 - t1;
        result.timing.getMs = t3 - t2;
        result.timing.outputConversionMs = t4 - t3;
    } else {
        BSetAny a, b;
        insertAny(a, lhs);
        insertAny(b, rhs);
        double t1 = nowMs();

        BSetAny out = applyBoolean(a, b, op);
        double t2 = nowMs();

        std::vector<BOutPoly> boostPolys = getBoostPolygons<BSetAny, BOutPoly>(out);
        double t3 = nowMs();

        result.polygons = convertBoostPolygonsToPointArrays(boostPolys);
        double t4 = nowMs();

        result.timing.inputConversionMs = t1 - t0;
        result.timing.booleanMs = t2 - t1;
        result.timing.getMs = t3 - t2;
        result.timing.outputConversionMs = t4 - t3;
    }
    return result;
}

struct Dataset { std::vector<PointArray> lhs; std::vector<PointArray> rhs; };

static Dataset makeDataset(int count, GeometryKind kind) {
    Dataset d;
    d.lhs.reserve(count);
    d.rhs.reserve(count);
    const Coord pitch = 40;
    const Coord shift = 7;
    for (int i = 0; i < count; ++i) {
        Coord x = static_cast<Coord>(i % 1000) * pitch;
        Coord y = static_cast<Coord>(i / 1000) * pitch;
        if (kind == GeometryKind::Polygon90) {
            if ((i % 10) == 0) {
                d.lhs.push_back(makeComb(x, y, 3, 8, 3));
                d.rhs.push_back(makeComb(x + shift, y + shift, 3, 8, 3));
            } else {
                d.lhs.push_back(makeRect(x, y, x + 16, y + 16));
                d.rhs.push_back(makeRect(x + shift, y + shift, x + shift + 16, y + shift + 16));
            }
        } else if (kind == GeometryKind::Polygon45) {
            if ((i % 2) == 0) {
                d.lhs.push_back(makeDiamond45(x + 16, y + 16, 12));
                d.rhs.push_back(makeDiamond45(x + 16 + shift, y + 16 + shift, 12));
            } else {
                d.lhs.push_back(makeRect(x, y, x + 20, y + 20));
                d.rhs.push_back(makeRect(x + shift, y + shift, x + shift + 20, y + shift + 20));
            }
        } else {
            if ((i % 2) == 0) {
                d.lhs.push_back(makeAnyTriangle(x, y, 23, 31));
                d.rhs.push_back(makeAnyTriangle(x + shift, y + shift, 23, 31));
            } else {
                d.lhs.push_back(makeDiamond45(x + 16, y + 16, 12));
                d.rhs.push_back(makeDiamond45(x + 16 + shift, y + 16 + shift, 12));
            }
        }
    }
    return d;
}

static double nowMs() {
    typedef std::chrono::high_resolution_clock Clock;
    static const Clock::time_point base = Clock::now();
    return std::chrono::duration<double, std::milli>(Clock::now() - base).count();
}

static std::size_t countPoints(const std::vector<PointArray>& polys) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < polys.size(); ++i) n += polys[i].numPoints;
    return n;
}

static void bench(const std::string& label, const Dataset& d, int rounds, std::size_t getVertexThreshold) {
    double best = std::numeric_limits<double>::max();
    BooleanResult bestResult;
    for (int r = 0; r < rounds; ++r) {
        BooleanResult current = booleanAuto(d.lhs, d.rhs, BooleanOp::Or, getVertexThreshold);
        const double total = current.timing.totalMs();
        if (total < best) {
            best = total;
            bestResult = std::move(current);
        }
    }
    std::cout << std::left << std::setw(20) << label
              << std::setw(24) << kindName(bestResult.engine)
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(11) << bestResult.timing.totalMs()
              << std::setw(11) << bestResult.timing.inputConversionMs
              << std::setw(11) << bestResult.timing.booleanMs
              << std::setw(11) << bestResult.timing.getMs
              << std::setw(11) << bestResult.timing.outputConversionMs
              << std::setw(12) << bestResult.polygons.size()
              << std::setw(12) << countPoints(bestResult.polygons) << '\n';
}

static void sanityCheck() {
    Dataset d90 = makeDataset(8, GeometryKind::Polygon90);
    Dataset d45 = makeDataset(8, GeometryKind::Polygon45);
    Dataset da = makeDataset(8, GeometryKind::AnyAngle);
    if (booleanAuto(d90.lhs, d90.rhs, BooleanOp::Or).engine != GeometryKind::Polygon90) throw std::runtime_error("90 dispatch failed");
    if (booleanAuto(d45.lhs, d45.rhs, BooleanOp::Or).engine != GeometryKind::Polygon45) throw std::runtime_error("45 dispatch failed");
    if (booleanAuto(da.lhs, da.rhs, BooleanOp::Or).engine != GeometryKind::AnyAngle) throw std::runtime_error("any-angle dispatch failed");
}

} // namespace pointarray_boolean

int main(int argc, char** argv) {
    try {
        int count = 1000000;
        int rounds = 1;
        std::size_t getVertexThreshold = 0;
        if (argc > 1) count = std::atoi(argv[1]);
        if (argc > 2) rounds = std::atoi(argv[2]);
        if (argc > 3) getVertexThreshold = static_cast<std::size_t>(std::strtoull(argv[3], NULL, 10));
        if (count <= 0 || rounds <= 0) {
            throw std::invalid_argument("usage: ./pointarray_boost_boolean_benchmark [polygon_count] [rounds] [polygon90_get_vertex_threshold]");
        }

        pointarray_boolean::sanityCheck();
        pointarray_boolean::Dataset d90 = pointarray_boolean::makeDataset(count, pointarray_boolean::GeometryKind::Polygon90);
        pointarray_boolean::Dataset d45 = pointarray_boolean::makeDataset(count, pointarray_boolean::GeometryKind::Polygon45);
        pointarray_boolean::Dataset any = pointarray_boolean::makeDataset(count, pointarray_boolean::GeometryKind::AnyAngle);

        std::cout << "PointArray Boost.Polygon auto-dispatch benchmark, op=OR, best-of=" << rounds << "\n";
        std::cout << "Timing columns are milliseconds: total = input conversion + boolean + get + output conversion.\n";
        std::cout << "input conversion includes geometry classification and PointArray->Boost insertion.\n";
        if (getVertexThreshold != 0) {
            std::cout << "polygon_90_set_data get vertex threshold=" << getVertexThreshold
                      << " (only the 90-degree engine supports this Boost overload).\n";
        }
        std::cout << std::left << std::setw(20) << "dataset"
                  << std::setw(24) << "selected engine"
                  << std::right << std::setw(11) << "total"
                  << std::setw(11) << "in_conv"
                  << std::setw(11) << "boolean"
                  << std::setw(11) << "get"
                  << std::setw(11) << "out_conv"
                  << std::setw(12) << "out polys"
                  << std::setw(12) << "out points" << '\n';
        pointarray_boolean::bench("90 rectilinear", d90, rounds, getVertexThreshold);
        pointarray_boolean::bench("45 mixed", d45, rounds, getVertexThreshold);
        pointarray_boolean::bench("any angle mixed", any, rounds, getVertexThreshold);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
