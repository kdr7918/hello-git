// boost_geometry_int64_boolean_rectclip.cpp
// C++11 예시
//
// g++ -std=c++11 boost_geometry_int64_boolean_rectclip.cpp -o test
//
// 구성:
// 1. Point / Ring / Poly / MultiPoly / Box
// 2. BoostPoint / BoostRing / BoostPoly / BoostMultiPoly
// 3. Boolean: Union, Xor, Difference, Intersection
// 4. Resize: Boost.Geometry buffer 사용
// 5. Hole 포함 contour flatten
// 6. RectClip:
//    - rectClipAccurate(): Boost intersection(box)
//    - rectClipRingFast(): Sutherland-Hodgman O(n) ring clip
//
// 주의:
// - Point는 int64_t 기반.
// - Boost.Geometry 연산은 int64_t point를 그대로 사용.
// - 다만 buffer/resize는 내부적으로 거리 계산 때문에 double distance 전략을 사용한다.
// - buffer 결과 좌표가 정수가 아닐 수 있으므로 최종 변환 시 llround()로 int64_t 변환한다.
// - Boost.Geometry는 outer와 hole을 하나의 closed contour로 자동 연결하지 않는다.
//   outer / hole을 각각 contour로 순회하는 방식으로 처리한다.

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/geometries/box.hpp>

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

namespace bg = boost::geometry;

// ============================================================
// 1. User geometry type
// ============================================================

struct Point
{
    int64_t x;
    int64_t y;

    Point() : x(0), y(0) {}
    Point(int64_t x_, int64_t y_) : x(x_), y(y_) {}
};

typedef std::vector<Point> Ring;

struct Poly
{
    Ring outer;
    std::vector<Ring> holes;
};

typedef std::vector<Poly> MultiPoly;

struct Box
{
    int64_t minX;
    int64_t minY;
    int64_t maxX;
    int64_t maxY;

    Box() : minX(0), minY(0), maxX(0), maxY(0) {}

    Box(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
        : minX(x1), minY(y1), maxX(x2), maxY(y2)
    {
        normalize();
    }

    void normalize()
    {
        if (minX > maxX)
            std::swap(minX, maxX);

        if (minY > maxY)
            std::swap(minY, maxY);
    }
};

// ============================================================
// 2. Boost.Geometry type
// ============================================================
//
// BoostPoint는 int64_t 좌표.
// polygon<BoostPoint, false, true>
// - false: counter-clockwise outer ring
// - true : closed ring
//
// bg::correct()를 호출하면 ring closure / orientation을 Boost 기준으로 보정한다.
//

typedef bg::model::d2::point_xy<int64_t> BoostPoint;
typedef bg::model::ring<BoostPoint, false, true> BoostRing;
typedef bg::model::polygon<BoostPoint, false, true> BoostPoly;
typedef bg::model::multi_polygon<BoostPoly> BoostMultiPoly;
typedef bg::model::box<BoostPoint> BoostBox;

// ============================================================
// 3. Utility
// ============================================================

static bool samePoint(const Point& a, const Point& b)
{
    return a.x == b.x && a.y == b.y;
}

static void closeRing(Ring& ring)
{
    if (ring.empty())
        return;

    if (!samePoint(ring.front(), ring.back()))
        ring.push_back(ring.front());
}

static void removeDuplicatedLastPoint(Ring& ring)
{
    if (ring.size() >= 2 && samePoint(ring.front(), ring.back()))
        ring.pop_back();
}

// signed area * 2
// area2 > 0 : CCW
// area2 < 0 : CW
//
// int64_t 좌표끼리 곱하면 overflow 가능성이 있으므로 long double 사용.
// EDA DBU 좌표가 매우 크면 __int128로 바꿔도 된다.
static long double signedArea2(const Ring& ring)
{
    if (ring.size() < 3)
        return 0.0L;

    Ring r = ring;
    removeDuplicatedLastPoint(r);

    long double a = 0.0L;
    const size_t n = r.size();

    for (size_t i = 0; i < n; ++i)
    {
        const Point& p = r[i];
        const Point& q = r[(i + 1) % n];

        a += static_cast<long double>(p.x) * static_cast<long double>(q.y)
           - static_cast<long double>(q.x) * static_cast<long double>(p.y);
    }

    return a;
}

static void reverseRing(Ring& ring)
{
    removeDuplicatedLastPoint(ring);
    std::reverse(ring.begin(), ring.end());
    closeRing(ring);
}

static void normalizePoly(Poly& poly)
{
    closeRing(poly.outer);

    // outer는 CCW 권장
    if (signedArea2(poly.outer) < 0)
        reverseRing(poly.outer);

    for (size_t i = 0; i < poly.holes.size(); ++i)
    {
        closeRing(poly.holes[i]);

        // hole은 CW 권장
        if (signedArea2(poly.holes[i]) > 0)
            reverseRing(poly.holes[i]);
    }
}

static int64_t roundToInt64(long double v)
{
    if (v > static_cast<long double>(std::numeric_limits<int64_t>::max()))
        return std::numeric_limits<int64_t>::max();

    if (v < static_cast<long double>(std::numeric_limits<int64_t>::min()))
        return std::numeric_limits<int64_t>::min();

    return static_cast<int64_t>(std::llround(v));
}

// ============================================================
// 4. Point conversion
// ============================================================

static BoostPoint toBoostPoint(const Point& p)
{
    return BoostPoint(p.x, p.y);
}

static Point toPoint(const BoostPoint& p)
{
    return Point(bg::get<0>(p), bg::get<1>(p));
}

// buffer 결과 등에서 double point를 int64 Point로 바꿀 때 사용
template <typename BoostPointT>
static Point toRoundedPoint(const BoostPointT& p)
{
    long double x = static_cast<long double>(bg::get<0>(p));
    long double y = static_cast<long double>(bg::get<1>(p));

    return Point(roundToInt64(x), roundToInt64(y));
}

// ============================================================
// 5. User geometry <-> Boost geometry conversion
// ============================================================

static BoostPoly toBoostPoly(Poly poly)
{
    normalizePoly(poly);

    BoostPoly bp;

    for (size_t i = 0; i < poly.outer.size(); ++i)
        bg::append(bp.outer(), toBoostPoint(poly.outer[i]));

    bp.inners().resize(poly.holes.size());

    for (size_t h = 0; h < poly.holes.size(); ++h)
    {
        for (size_t i = 0; i < poly.holes[h].size(); ++i)
            bg::append(bp.inners()[h], toBoostPoint(poly.holes[h][i]));
    }

    bg::correct(bp);
    return bp;
}

static BoostMultiPoly toBoostMultiPoly(const MultiPoly& mp)
{
    BoostMultiPoly out;

    for (size_t i = 0; i < mp.size(); ++i)
        out.push_back(toBoostPoly(mp[i]));

    bg::correct(out);
    return out;
}

static Ring toRing(const BoostRing& br)
{
    Ring out;

    for (size_t i = 0; i < br.size(); ++i)
        out.push_back(toPoint(br[i]));

    closeRing(out);
    return out;
}

static Poly toPoly(const BoostPoly& bp)
{
    Poly out;

    out.outer = toRing(bp.outer());

    for (size_t h = 0; h < bp.inners().size(); ++h)
        out.holes.push_back(toRing(bp.inners()[h]));

    normalizePoly(out);
    return out;
}

static MultiPoly toMultiPoly(const BoostMultiPoly& bmp)
{
    MultiPoly out;

    for (size_t i = 0; i < bmp.size(); ++i)
        out.push_back(toPoly(bmp[i]));

    return out;
}

// ============================================================
// 6. Boolean operations
// ============================================================

enum class BoolOp
{
    UnionOp,
    XorOp,
    DifferenceOp,
    IntersectionOp
};

static MultiPoly booleanOp(
    const MultiPoly& a,
    const MultiPoly& b,
    BoolOp op)
{
    BoostMultiPoly ba = toBoostMultiPoly(a);
    BoostMultiPoly bb = toBoostMultiPoly(b);
    BoostMultiPoly result;

    switch (op)
    {
    case BoolOp::UnionOp:
        bg::union_(ba, bb, result);
        break;

    case BoolOp::XorOp:
        bg::sym_difference(ba, bb, result);
        break;

    case BoolOp::DifferenceOp:
        bg::difference(ba, bb, result);
        break;

    case BoolOp::IntersectionOp:
        bg::intersection(ba, bb, result);
        break;
    }

    bg::correct(result);
    return toMultiPoly(result);
}

static MultiPoly booleanUnion(const MultiPoly& a, const MultiPoly& b)
{
    return booleanOp(a, b, BoolOp::UnionOp);
}

static MultiPoly booleanXor(const MultiPoly& a, const MultiPoly& b)
{
    return booleanOp(a, b, BoolOp::XorOp);
}

static MultiPoly booleanDiff(const MultiPoly& a, const MultiPoly& b)
{
    return booleanOp(a, b, BoolOp::DifferenceOp);
}

static MultiPoly booleanIntersection(const MultiPoly& a, const MultiPoly& b)
{
    return booleanOp(a, b, BoolOp::IntersectionOp);
}

// ============================================================
// 7. Resize / Offset
// ============================================================
//
// int64_t polygon을 입력받고, buffer는 double 기반 point로 수행한 뒤
// 결과를 int64_t로 반올림한다.
//
// 이유:
// - Boost buffer에서 int64_t point + 정수 좌표 결과만 기대하면
//   대각선/mitre/round join 등에서 좌표가 애매해질 수 있다.
// - EDA에서는 보통 DBU grid로 최종 snap해야 하므로,
//   double 결과를 llround()로 grid snap하는 식이 실용적이다.
//

typedef bg::model::d2::point_xy<double> BoostDPoint;
typedef bg::model::ring<BoostDPoint, false, true> BoostDRing;
typedef bg::model::polygon<BoostDPoint, false, true> BoostDPoly;
typedef bg::model::multi_polygon<BoostDPoly> BoostDMultiPoly;

static BoostDPoint toBoostDPoint(const Point& p)
{
    return BoostDPoint(static_cast<double>(p.x), static_cast<double>(p.y));
}

static BoostDPoly toBoostDPoly(Poly poly)
{
    normalizePoly(poly);

    BoostDPoly bp;

    for (size_t i = 0; i < poly.outer.size(); ++i)
        bg::append(bp.outer(), toBoostDPoint(poly.outer[i]));

    bp.inners().resize(poly.holes.size());

    for (size_t h = 0; h < poly.holes.size(); ++h)
    {
        for (size_t i = 0; i < poly.holes[h].size(); ++i)
            bg::append(bp.inners()[h], toBoostDPoint(poly.holes[h][i]));
    }

    bg::correct(bp);
    return bp;
}

static BoostDMultiPoly toBoostDMultiPoly(const MultiPoly& mp)
{
    BoostDMultiPoly out;

    for (size_t i = 0; i < mp.size(); ++i)
        out.push_back(toBoostDPoly(mp[i]));

    bg::correct(out);
    return out;
}

static Ring toRoundedRing(const BoostDRing& br)
{
    Ring out;

    for (size_t i = 0; i < br.size(); ++i)
        out.push_back(toRoundedPoint(br[i]));

    closeRing(out);
    return out;
}

static Poly toRoundedPoly(const BoostDPoly& bp)
{
    Poly out;

    out.outer = toRoundedRing(bp.outer());

    for (size_t h = 0; h < bp.inners().size(); ++h)
        out.holes.push_back(toRoundedRing(bp.inners()[h]));

    normalizePoly(out);
    return out;
}

static MultiPoly toRoundedMultiPoly(const BoostDMultiPoly& bmp)
{
    MultiPoly out;

    for (size_t i = 0; i < bmp.size(); ++i)
        out.push_back(toRoundedPoly(bmp[i]));

    return out;
}

static MultiPoly resizePoly(
    const MultiPoly& input,
    double delta,
    double miterLimit = 5.0)
{
    BoostDMultiPoly bin = toBoostDMultiPoly(input);
    BoostDMultiPoly bout;

    namespace bs = boost::geometry::strategy::buffer;

    bs::distance_symmetric<double> distanceStrategy(delta);
    bs::side_straight sideStrategy;
    bs::join_miter joinStrategy(miterLimit);

    // polygon에는 큰 의미 없음
    bs::end_flat endStrategy;

    // point geometry용 전략. polygon에서도 template 인자로 필요.
    bs::point_square pointStrategy;

    bg::buffer(
        bin,
        bout,
        distanceStrategy,
        sideStrategy,
        joinStrategy,
        endStrategy,
        pointStrategy
    );

    bg::correct(bout);
    return toRoundedMultiPoly(bout);
}

// ============================================================
// 8. Hole 포함 contour flatten
// ============================================================
//
// Boost.Geometry는 outer와 hole을 하나의 단일 ring으로 연결하지 않는다.
// 따라서 아래처럼 outer / hole을 contour 단위로 펼쳐서 순회한다.
//
// 진짜 "한붓그리기"가 필요하면,
// hole과 outer 사이에 bridge segment를 삽입하는 알고리즘이 별도로 필요하다.
//

struct StrokeContour
{
    Ring ring;
    bool isHole;
    size_t polyIndex;
    size_t holeIndex; // isHole == false면 0
};

static std::vector<StrokeContour> flattenContoursForStroke(const MultiPoly& mp)
{
    std::vector<StrokeContour> out;

    for (size_t p = 0; p < mp.size(); ++p)
    {
        Poly poly = mp[p];
        normalizePoly(poly);

        StrokeContour outer;
        outer.ring = poly.outer;
        outer.isHole = false;
        outer.polyIndex = p;
        outer.holeIndex = 0;
        out.push_back(outer);

        for (size_t h = 0; h < poly.holes.size(); ++h)
        {
            StrokeContour hole;
            hole.ring = poly.holes[h];
            hole.isHole = true;
            hole.polyIndex = p;
            hole.holeIndex = h;
            out.push_back(hole);
        }
    }

    return out;
}

// ============================================================
// 9. RectClip - 정확한 버전
// ============================================================
//
// polygon/hole topology를 정확히 보존하려면 이 방식을 권장.
// Boost intersection으로 box와 polygon을 교차시킨다.
//

static MultiPoly rectClipAccurate(
    const MultiPoly& input,
    Box box)
{
    box.normalize();

    BoostMultiPoly bin = toBoostMultiPoly(input);
    BoostBox bbox(
        BoostPoint(box.minX, box.minY),
        BoostPoint(box.maxX, box.maxY)
    );

    BoostMultiPoly bout;

    bg::intersection(bin, bbox, bout);
    bg::correct(bout);

    return toMultiPoly(bout);
}

// ============================================================
// 10. RectClip - 빠른 ring 단위 O(n) 버전
// ============================================================
//
// Sutherland-Hodgman polygon clipping.
// 단일 Ring을 axis-aligned Box로 clipping한다.
//
// 장점:
// - O(n)
// - 타입 변환 거의 없음
// - contour 렌더링/단순 path clipping에는 빠름
//
// 단점:
// - polygon + hole topology를 자동 재구성하지 않는다.
// - hole이 box 경계에 의해 잘리는 경우 정확한 polygon 구조로 만들지 않는다.
// - 정확한 결과가 필요하면 rectClipAccurate() 사용.
//

enum class ClipEdge
{
    Left,
    Right,
    Bottom,
    Top
};

static bool insideEdge(const Point& p, const Box& r, ClipEdge e)
{
    switch (e)
    {
    case ClipEdge::Left:
        return p.x >= r.minX;

    case ClipEdge::Right:
        return p.x <= r.maxX;

    case ClipEdge::Bottom:
        return p.y >= r.minY;

    case ClipEdge::Top:
        return p.y <= r.maxY;
    }

    return false;
}

static Point intersectEdge(
    const Point& a,
    const Point& b,
    const Box& r,
    ClipEdge e)
{
    const long double ax = static_cast<long double>(a.x);
    const long double ay = static_cast<long double>(a.y);
    const long double bx = static_cast<long double>(b.x);
    const long double by = static_cast<long double>(b.y);

    const long double dx = bx - ax;
    const long double dy = by - ay;

    long double t = 0.0L;

    switch (e)
    {
    case ClipEdge::Left:
        // x = minX
        if (dx == 0.0L)
            return Point(r.minX, a.y);

        t = (static_cast<long double>(r.minX) - ax) / dx;
        return Point(
            r.minX,
            roundToInt64(ay + t * dy)
        );

    case ClipEdge::Right:
        // x = maxX
        if (dx == 0.0L)
            return Point(r.maxX, a.y);

        t = (static_cast<long double>(r.maxX) - ax) / dx;
        return Point(
            r.maxX,
            roundToInt64(ay + t * dy)
        );

    case ClipEdge::Bottom:
        // y = minY
        if (dy == 0.0L)
            return Point(a.x, r.minY);

        t = (static_cast<long double>(r.minY) - ay) / dy;
        return Point(
            roundToInt64(ax + t * dx),
            r.minY
        );

    case ClipEdge::Top:
        // y = maxY
        if (dy == 0.0L)
            return Point(a.x, r.maxY);

        t = (static_cast<long double>(r.maxY) - ay) / dy;
        return Point(
            roundToInt64(ax + t * dx),
            r.maxY
        );
    }

    return a;
}

static Ring clipRingByOneEdge(
    const Ring& inputRing,
    const Box& box,
    ClipEdge edge)
{
    Ring in = inputRing;
    removeDuplicatedLastPoint(in);

    Ring out;

    if (in.empty())
        return out;

    Point prev = in.back();
    bool prevInside = insideEdge(prev, box, edge);

    for (size_t i = 0; i < in.size(); ++i)
    {
        Point curr = in[i];
        bool currInside = insideEdge(curr, box, edge);

        if (prevInside && currInside)
        {
            // inside -> inside
            out.push_back(curr);
        }
        else if (prevInside && !currInside)
        {
            // inside -> outside
            out.push_back(intersectEdge(prev, curr, box, edge));
        }
        else if (!prevInside && currInside)
        {
            // outside -> inside
            out.push_back(intersectEdge(prev, curr, box, edge));
            out.push_back(curr);
        }
        else
        {
            // outside -> outside
        }

        prev = curr;
        prevInside = currInside;
    }

    closeRing(out);
    return out;
}

static Ring rectClipRingFast(
    const Ring& ring,
    Box box)
{
    box.normalize();

    Ring out = ring;

    out = clipRingByOneEdge(out, box, ClipEdge::Left);
    if (out.size() < 4)
        return Ring();

    out = clipRingByOneEdge(out, box, ClipEdge::Right);
    if (out.size() < 4)
        return Ring();

    out = clipRingByOneEdge(out, box, ClipEdge::Bottom);
    if (out.size() < 4)
        return Ring();

    out = clipRingByOneEdge(out, box, ClipEdge::Top);
    if (out.size() < 4)
        return Ring();

    closeRing(out);
    return out;
}

static std::vector<StrokeContour> rectClipFlattenContoursFast(
    const std::vector<StrokeContour>& contours,
    Box box)
{
    box.normalize();

    std::vector<StrokeContour> out;

    for (size_t i = 0; i < contours.size(); ++i)
    {
        StrokeContour c = contours[i];
        c.ring = rectClipRingFast(c.ring, box);

        if (c.ring.size() >= 4)
            out.push_back(c);
    }

    return out;
}

// ============================================================
// 11. Optional helper: simple Box to Poly
// ============================================================

static Poly makeBoxPoly(Box box)
{
    box.normalize();

    Poly p;

    p.outer.push_back(Point(box.minX, box.minY));
    p.outer.push_back(Point(box.maxX, box.minY));
    p.outer.push_back(Point(box.maxX, box.maxY));
    p.outer.push_back(Point(box.minX, box.maxY));
    closeRing(p.outer);

    normalizePoly(p);
    return p;
}

// ============================================================
// 12. Print helpers
// ============================================================

static void printRing(const Ring& ring)
{
    std::cout << "Ring size = " << ring.size() << "\n";

    for (size_t i = 0; i < ring.size(); ++i)
    {
        std::cout << "  (" << ring[i].x << ", " << ring[i].y << ")\n";
    }
}

static void printMultiPoly(const MultiPoly& mp, const char* title)
{
    std::cout << "\n=== " << title << " ===\n";
    std::cout << "poly count = " << mp.size() << "\n";

    for (size_t i = 0; i < mp.size(); ++i)
    {
        std::cout << "poly[" << i << "] outer:\n";
        printRing(mp[i].outer);

        for (size_t h = 0; h < mp[i].holes.size(); ++h)
        {
            std::cout << "poly[" << i << "] hole[" << h << "]:\n";
            printRing(mp[i].holes[h]);
        }
    }
}

// ============================================================
// 13. Example
// ============================================================

int main()
{
    // A: 0,0 ~ 100,100 사각형 + 가운데 hole
    Poly polyA;

    polyA.outer.push_back(Point(0, 0));
    polyA.outer.push_back(Point(100, 0));
    polyA.outer.push_back(Point(100, 100));
    polyA.outer.push_back(Point(0, 100));
    closeRing(polyA.outer);

    Ring holeA;
    holeA.push_back(Point(40, 40));
    holeA.push_back(Point(60, 40));
    holeA.push_back(Point(60, 60));
    holeA.push_back(Point(40, 60));
    closeRing(holeA);

    polyA.holes.push_back(holeA);

    MultiPoly A;
    A.push_back(polyA);

    // B: 50,50 ~ 150,150 사각형
    Poly polyB = makeBoxPoly(Box(50, 50, 150, 150));

    MultiPoly B;
    B.push_back(polyB);

    MultiPoly u = booleanUnion(A, B);
    MultiPoly x = booleanXor(A, B);
    MultiPoly d = booleanDiff(A, B);
    MultiPoly inter = booleanIntersection(A, B);

    printMultiPoly(u, "UNION");
    printMultiPoly(x, "XOR");
    printMultiPoly(d, "DIFF A-B");
    printMultiPoly(inter, "INTERSECTION");

    // Resize
    MultiPoly grown = resizePoly(A, 5.0, 5.0);
    MultiPoly shrunk = resizePoly(A, -5.0, 5.0);

    printMultiPoly(grown, "RESIZE +5");
    printMultiPoly(shrunk, "RESIZE -5");

    // Hole 포함 contour flatten
    std::vector<StrokeContour> contours = flattenContoursForStroke(A);

    std::cout << "\n=== FLATTEN CONTOURS ===\n";
    for (size_t c = 0; c < contours.size(); ++c)
    {
        std::cout << "contour[" << c << "] "
                  << (contours[c].isHole ? "HOLE" : "OUTER")
                  << ", polyIndex=" << contours[c].polyIndex
                  << ", holeIndex=" << contours[c].holeIndex
                  << "\n";

        printRing(contours[c].ring);
    }

    // RectClip 정확한 버전
    Box clipBox(25, 25, 75, 75);

    MultiPoly clippedAccurate = rectClipAccurate(A, clipBox);
    printMultiPoly(clippedAccurate, "RECT CLIP ACCURATE");

    // RectClip 빠른 contour 버전
    std::vector<StrokeContour> clippedContours =
        rectClipFlattenContoursFast(contours, clipBox);

    std::cout << "\n=== RECT CLIP FAST CONTOURS ===\n";
    for (size_t c = 0; c < clippedContours.size(); ++c)
    {
        std::cout << "clipped contour[" << c << "] "
                  << (clippedContours[c].isHole ? "HOLE" : "OUTER")
                  << "\n";

        printRing(clippedContours[c].ring);
    }

    return 0;
}
