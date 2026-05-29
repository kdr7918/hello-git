// boost_geometry_direct_functions_int64.cpp
// C++11 기준
//
// 목적:
// - 변환 함수 없이 point / Box / BoostRing / BoostPoly / BoostMultiPoly 기준으로 바로 동작
// - Boolean: OR, XOR, DIFF, INTERSECTION
// - Resize: Boost.Geometry buffer
// - Hole 포함 contour flatten
// - Boolean 결과를 rectClip 후 bridge 기반 한붓그리기 path로 변환
// - RectClip:
//   1) rectClipAccurate(): Boost intersection 기반, topology 보존
//   2) rectClipRingFast(): Ring 단위 O(n) Sutherland-Hodgman
//
// 전제:
// - point는 int64_t x, y를 가진 유저 객체
// - Boost.Geometry에 point를 등록
// - BoostRing / BoostPoly / BoostMultiPoly는 point 기반 Boost.Geometry 타입
//
// 주의:
// - Boost.Geometry는 outer + hole을 하나의 ring으로 자동 연결하지 않는다.
// - flattenContoursForStroke()는 outer/hole contour를 따로 순회한다.
// - makeSingleStrokePaths()는 hole마다 bridge edge를 왕복 삽입해 하나의 closed walk를 만든다.
// - bridge 후보는 outer/hole/current path와의 교차를 검사해 hole을 자르는 선을 피한다.

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/ring.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace bg = boost::geometry;

// ============================================================
// 1. User objects
// ============================================================

struct point
{
    int64_t x;
    int64_t y;

    point() : x(0), y(0) {}
    point(int64_t x_, int64_t y_) : x(x_), y(y_) {}
};

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

// point 등록
// 이미 프로젝트 어딘가에서 등록했다면 이 줄은 중복 등록되므로 제거.
BOOST_GEOMETRY_REGISTER_POINT_2D(point, int64_t, bg::cs::cartesian, x, y)

// ============================================================
// 2. Boost.Geometry objects
// ============================================================
//
// false = outer ring CCW
// true  = closed ring
//
// bg::correct() 호출 시 orientation/closure를 Boost 기준으로 보정한다.
//

typedef bg::model::ring<point, false, true> BoostRing;
typedef bg::model::polygon<point, false, true> BoostPoly;
typedef bg::model::multi_polygon<BoostPoly> BoostMultiPoly;

// ============================================================
// 3. Basic utilities
// ============================================================

static bool samePoint(const point& a, const point& b)
{
    return a.x == b.x && a.y == b.y;
}

static void closeRing(BoostRing& ring)
{
    if (ring.empty())
        return;

    if (!samePoint(ring.front(), ring.back()))
        ring.push_back(ring.front());
}

static void removeDuplicatedLastPoint(BoostRing& ring)
{
    if (ring.size() >= 2 && samePoint(ring.front(), ring.back()))
        ring.pop_back();
}

static long double signedArea2(const BoostRing& ring)
{
    if (ring.size() < 3)
        return 0.0L;

    BoostRing r = ring;
    removeDuplicatedLastPoint(r);

    long double a = 0.0L;
    const size_t n = r.size();

    for (size_t i = 0; i < n; ++i)
    {
        const point& p = r[i];
        const point& q = r[(i + 1) % n];

        a += static_cast<long double>(p.x) * static_cast<long double>(q.y)
           - static_cast<long double>(q.x) * static_cast<long double>(p.y);
    }

    return a;
}

static void reverseRing(BoostRing& ring)
{
    removeDuplicatedLastPoint(ring);
    std::reverse(ring.begin(), ring.end());
    closeRing(ring);
}

static void normalizeBoostPoly(BoostPoly& poly)
{
    closeRing(poly.outer());

    // polygon<point, false, true> 기준:
    // outer는 CCW 권장
    if (signedArea2(poly.outer()) < 0)
        reverseRing(poly.outer());

    for (size_t i = 0; i < poly.inners().size(); ++i)
    {
        closeRing(poly.inners()[i]);

        // hole은 CW 권장
        if (signedArea2(poly.inners()[i]) > 0)
            reverseRing(poly.inners()[i]);
    }

    bg::correct(poly);
}

static void normalizeBoostMultiPoly(BoostMultiPoly& mp)
{
    for (size_t i = 0; i < mp.size(); ++i)
        normalizeBoostPoly(mp[i]);

    bg::correct(mp);
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
// 4. Boolean operations
// ============================================================

enum class BoolOp
{
    UnionOp,
    XorOp,
    DifferenceOp,
    IntersectionOp
};

static BoostMultiPoly booleanOp(
    BoostMultiPoly a,
    BoostMultiPoly b,
    BoolOp op)
{
    normalizeBoostMultiPoly(a);
    normalizeBoostMultiPoly(b);

    BoostMultiPoly result;

    switch (op)
    {
    case BoolOp::UnionOp:
        bg::union_(a, b, result);
        break;

    case BoolOp::XorOp:
        bg::sym_difference(a, b, result);
        break;

    case BoolOp::DifferenceOp:
        bg::difference(a, b, result);
        break;

    case BoolOp::IntersectionOp:
        bg::intersection(a, b, result);
        break;
    }

    normalizeBoostMultiPoly(result);
    return result;
}

static BoostMultiPoly booleanUnion(
    const BoostMultiPoly& a,
    const BoostMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::UnionOp);
}

static BoostMultiPoly booleanXor(
    const BoostMultiPoly& a,
    const BoostMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::XorOp);
}

static BoostMultiPoly booleanDiff(
    const BoostMultiPoly& a,
    const BoostMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::DifferenceOp);
}

static BoostMultiPoly booleanIntersection(
    const BoostMultiPoly& a,
    const BoostMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::IntersectionOp);
}

// ============================================================
// 5. Resize / Offset
// ============================================================
//
// Boost.Geometry buffer를 BoostMultiPoly에 직접 적용한다.
//
// delta > 0 : 확장
// delta < 0 : 축소
//
// 주의:
// - point 좌표 타입이 int64_t라서 buffer 결과가 정수 좌표로 들어간다.
// - 대각선, miter, round 계열에서는 내부 계산 결과가 정수가 아닐 수 있다.
// - EDA DBU 목적이면 보통 이 방식도 실용적이지만,
//   아주 정밀한 offset이 필요하면 double 중간 타입 + grid snap 방식이 더 안전하다.
//

static BoostMultiPoly resizePoly(
    BoostMultiPoly input,
    double delta,
    double miterLimit = 5.0)
{
    normalizeBoostMultiPoly(input);

    BoostMultiPoly result;

    namespace bs = boost::geometry::strategy::buffer;

    bs::distance_symmetric<double> distanceStrategy(delta);
    bs::side_straight sideStrategy;
    bs::join_miter joinStrategy(miterLimit);

    // polygon buffer에서는 end strategy는 큰 의미 없음
    bs::end_flat endStrategy;

    // point geometry용 전략. polygon buffer에도 template 인자로 필요.
    bs::point_square pointStrategy;

    bg::buffer(
        input,
        result,
        distanceStrategy,
        sideStrategy,
        joinStrategy,
        endStrategy,
        pointStrategy
    );

    normalizeBoostMultiPoly(result);
    return result;
}

// ============================================================
// 6. Hole 포함 contour flatten
// ============================================================
//
// Boost.Geometry는 outer와 hole을 하나의 ring으로 합쳐주지 않는다.
// 그래서 outer/hole을 StrokeContour 목록으로 펼쳐서 순회한다.
//

struct StrokeContour
{
    BoostRing ring;
    bool isHole;
    size_t polyIndex;
    size_t holeIndex; // isHole == false면 0
};

static BoostMultiPoly rectClipAccurate(const BoostMultiPoly& input, Box clipBox);

static std::vector<StrokeContour> flattenContoursForStroke(
    BoostMultiPoly mp)
{
    normalizeBoostMultiPoly(mp);

    std::vector<StrokeContour> out;

    for (size_t p = 0; p < mp.size(); ++p)
    {
        StrokeContour outer;
        outer.ring = mp[p].outer();
        outer.isHole = false;
        outer.polyIndex = p;
        outer.holeIndex = 0;
        out.push_back(outer);

        for (size_t h = 0; h < mp[p].inners().size(); ++h)
        {
            StrokeContour hole;
            hole.ring = mp[p].inners()[h];
            hole.isHole = true;
            hole.polyIndex = p;
            hole.holeIndex = h;
            out.push_back(hole);
        }
    }

    return out;
}

// ============================================================
// 7. Hole bridge: one-stroke closed walk
// ============================================================
//
// flattenContoursForStroke()는 outer/hole을 따로 순회한다.
// 아래 함수들은 하나의 polygon 안에서 outer와 모든 hole을 bridge edge로
// 연결해 "한붓그리기용 closed walk" 하나를 만든다.
//
// 결과 path에는 bridge가 왕복으로 들어간다.
//
//   outer ... A -> H -> hole contour -> H -> A ... outer
//
// bridge 후보는 다음 조건을 통과해야 한다.
// - bridge 중점이 polygon 내부이며 어떤 hole 내부도 아니어야 한다.
// - bridge가 outer 경계, 다른 hole 경계, 이미 삽입된 path를 가로지르지 않아야 한다.
// - endpoint에서 만나는 것은 허용한다.
//
// 일반 self-touching/복잡 polygon까지 완전히 처리하는 범용 triangulation은 아니지만,
// EDA 직교 polygon/hole에서 "hole을 자르지 않는" bridge를 고르는 실용 버전이다.
//

struct RealPoint
{
    long double x;
    long double y;

    RealPoint() : x(0), y(0) {}
    RealPoint(long double x_, long double y_) : x(x_), y(y_) {}
};

static bool sameRealPoint(const RealPoint& a, const RealPoint& b)
{
    const long double eps = 1e-12L;
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

static RealPoint toRealPoint(const point& p)
{
    return RealPoint(
        static_cast<long double>(p.x),
        static_cast<long double>(p.y)
    );
}

static long double cross(
    const RealPoint& a,
    const RealPoint& b,
    const RealPoint& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool onSegment(
    const RealPoint& a,
    const RealPoint& b,
    const RealPoint& p)
{
    const long double eps = 1e-12L;

    if (std::fabs(cross(a, b, p)) > eps)
        return false;

    return p.x >= std::min(a.x, b.x) - eps &&
           p.x <= std::max(a.x, b.x) + eps &&
           p.y >= std::min(a.y, b.y) - eps &&
           p.y <= std::max(a.y, b.y) + eps;
}

static bool segmentsIntersect(
    const RealPoint& a,
    const RealPoint& b,
    const RealPoint& c,
    const RealPoint& d)
{
    const long double eps = 1e-12L;
    const long double c1 = cross(a, b, c);
    const long double c2 = cross(a, b, d);
    const long double c3 = cross(c, d, a);
    const long double c4 = cross(c, d, b);

    if (((c1 > eps && c2 < -eps) || (c1 < -eps && c2 > eps)) &&
        ((c3 > eps && c4 < -eps) || (c3 < -eps && c4 > eps)))
        return true;

    if (std::fabs(c1) <= eps && onSegment(a, b, c)) return true;
    if (std::fabs(c2) <= eps && onSegment(a, b, d)) return true;
    if (std::fabs(c3) <= eps && onSegment(c, d, a)) return true;
    if (std::fabs(c4) <= eps && onSegment(c, d, b)) return true;

    return false;
}

static bool intersectionIsOnlyAllowedEndpoint(
    const RealPoint& bridgeA,
    const RealPoint& bridgeB,
    const RealPoint& edgeA,
    const RealPoint& edgeB,
    const RealPoint& allowedA,
    const RealPoint& allowedB)
{
    const bool bridgeAAllowed =
        sameRealPoint(bridgeA, allowedA) || sameRealPoint(bridgeA, allowedB);
    const bool bridgeBAllowed =
        sameRealPoint(bridgeB, allowedA) || sameRealPoint(bridgeB, allowedB);

    if (bridgeAAllowed &&
        (sameRealPoint(bridgeA, edgeA) || sameRealPoint(bridgeA, edgeB)))
        return true;

    if (bridgeBAllowed &&
        (sameRealPoint(bridgeB, edgeA) || sameRealPoint(bridgeB, edgeB)))
        return true;

    return false;
}

static bool segmentHitsRingExceptAllowedEndpoints(
    const point& a,
    const point& b,
    BoostRing ring,
    const point& allowedA,
    const point& allowedB)
{
    removeDuplicatedLastPoint(ring);

    if (ring.size() < 2)
        return false;

    const RealPoint ba = toRealPoint(a);
    const RealPoint bb = toRealPoint(b);
    const RealPoint aa = toRealPoint(allowedA);
    const RealPoint ab = toRealPoint(allowedB);

    for (size_t i = 0; i < ring.size(); ++i)
    {
        const point& e0p = ring[i];
        const point& e1p = ring[(i + 1) % ring.size()];

        if (samePoint(e0p, e1p))
            continue;

        const RealPoint e0 = toRealPoint(e0p);
        const RealPoint e1 = toRealPoint(e1p);

        if (!segmentsIntersect(ba, bb, e0, e1))
            continue;

        if (intersectionIsOnlyAllowedEndpoint(ba, bb, e0, e1, aa, ab))
            continue;

        return true;
    }

    return false;
}

static bool pointInsideRing(const RealPoint& p, BoostRing ring)
{
    removeDuplicatedLastPoint(ring);

    if (ring.size() < 3)
        return false;

    bool inside = false;

    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
    {
        const RealPoint pi = toRealPoint(ring[i]);
        const RealPoint pj = toRealPoint(ring[j]);

        if (onSegment(pj, pi, p))
            return true;

        const bool crosses =
            ((pi.y > p.y) != (pj.y > p.y)) &&
            (p.x < (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x);

        if (crosses)
            inside = !inside;
    }

    return inside;
}

static bool pointInsidePolyArea(const RealPoint& p, const BoostPoly& poly)
{
    if (!pointInsideRing(p, poly.outer()))
        return false;

    for (size_t i = 0; i < poly.inners().size(); ++i)
    {
        if (pointInsideRing(p, poly.inners()[i]))
            return false;
    }

    return true;
}

static long double distance2(const point& a, const point& b)
{
    const long double dx =
        static_cast<long double>(a.x) - static_cast<long double>(b.x);
    const long double dy =
        static_cast<long double>(a.y) - static_cast<long double>(b.y);

    return dx * dx + dy * dy;
}

static bool validBridge(
    const BoostPoly& poly,
    const BoostRing& currentPath,
    size_t holeIndex,
    const point& pathPoint,
    const point& holePoint)
{
    if (samePoint(pathPoint, holePoint))
        return false;

    const RealPoint mid(
        (static_cast<long double>(pathPoint.x) + static_cast<long double>(holePoint.x)) * 0.5L,
        (static_cast<long double>(pathPoint.y) + static_cast<long double>(holePoint.y)) * 0.5L
    );

    if (!pointInsidePolyArea(mid, poly))
        return false;

    if (segmentHitsRingExceptAllowedEndpoints(
            pathPoint,
            holePoint,
            poly.outer(),
            pathPoint,
            holePoint))
        return false;

    for (size_t h = 0; h < poly.inners().size(); ++h)
    {
        if (segmentHitsRingExceptAllowedEndpoints(
                pathPoint,
                holePoint,
                poly.inners()[h],
                pathPoint,
                h == holeIndex ? holePoint : pathPoint))
            return false;
    }

    if (segmentHitsRingExceptAllowedEndpoints(
            pathPoint,
            holePoint,
            currentPath,
            pathPoint,
            holePoint))
        return false;

    return true;
}

static void spliceHoleIntoPath(
    BoostRing& currentPath,
    size_t pathIndex,
    BoostRing hole,
    size_t holeIndex)
{
    removeDuplicatedLastPoint(currentPath);
    removeDuplicatedLastPoint(hole);

    BoostRing merged;

    for (size_t i = 0; i <= pathIndex; ++i)
        merged.push_back(currentPath[i]);

    for (size_t i = 0; i < hole.size(); ++i)
        merged.push_back(hole[(holeIndex + i) % hole.size()]);

    merged.push_back(hole[holeIndex]);
    merged.push_back(currentPath[pathIndex]);

    for (size_t i = pathIndex + 1; i < currentPath.size(); ++i)
        merged.push_back(currentPath[i]);

    closeRing(merged);
    currentPath = merged;
}

static BoostRing makeSingleStrokePathForPoly(BoostPoly poly)
{
    normalizeBoostPoly(poly);

    BoostRing path = poly.outer();
    removeDuplicatedLastPoint(path);

    if (path.size() < 3)
        return BoostRing();

    std::vector<bool> connected(poly.inners().size(), false);

    for (size_t connectedCount = 0; connectedCount < poly.inners().size(); ++connectedCount)
    {
        bool found = false;
        size_t bestHole = 0;
        size_t bestPathIndex = 0;
        size_t bestHoleIndex = 0;
        long double bestDist = 0.0L;

        for (size_t h = 0; h < poly.inners().size(); ++h)
        {
            if (connected[h])
                continue;

            BoostRing hole = poly.inners()[h];
            removeDuplicatedLastPoint(hole);

            if (hole.size() < 3)
                continue;

            for (size_t pi = 0; pi < path.size(); ++pi)
            {
                for (size_t hi = 0; hi < hole.size(); ++hi)
                {
                    if (!validBridge(poly, path, h, path[pi], hole[hi]))
                        continue;

                    const long double d = distance2(path[pi], hole[hi]);

                    if (!found || d < bestDist)
                    {
                        found = true;
                        bestDist = d;
                        bestHole = h;
                        bestPathIndex = pi;
                        bestHoleIndex = hi;
                    }
                }
            }
        }

        if (!found)
            throw std::runtime_error("failed to find a non-crossing bridge for a hole");

        spliceHoleIntoPath(path, bestPathIndex, poly.inners()[bestHole], bestHoleIndex);
        removeDuplicatedLastPoint(path);
        connected[bestHole] = true;
    }

    closeRing(path);
    return path;
}

static std::vector<BoostRing> makeSingleStrokePaths(BoostMultiPoly mp)
{
    normalizeBoostMultiPoly(mp);

    std::vector<BoostRing> out;

    for (size_t i = 0; i < mp.size(); ++i)
    {
        BoostRing path = makeSingleStrokePathForPoly(mp[i]);

        if (path.size() >= 4)
            out.push_back(path);
    }

    return out;
}

static std::vector<BoostRing> booleanRectClipSingleStrokePaths(
    const BoostMultiPoly& a,
    const BoostMultiPoly& b,
    BoolOp op,
    Box clipBox)
{
    BoostMultiPoly booleanResult = booleanOp(a, b, op);
    BoostMultiPoly clippedResult = rectClipAccurate(booleanResult, clipBox);
    return makeSingleStrokePaths(clippedResult);
}

// ============================================================
// 8. Box -> BoostPoly
// ============================================================
//
// Boost.Geometry box 타입을 따로 쓰지 않고,
// 요청한 객체 기준대로 Box를 BoostPoly 사각형으로 만든다.
//

static BoostPoly makeBoxPoly(Box box)
{
    box.normalize();

    BoostPoly poly;

    poly.outer().push_back(point(box.minX, box.minY));
    poly.outer().push_back(point(box.maxX, box.minY));
    poly.outer().push_back(point(box.maxX, box.maxY));
    poly.outer().push_back(point(box.minX, box.maxY));
    poly.outer().push_back(point(box.minX, box.minY));

    normalizeBoostPoly(poly);
    return poly;
}

static BoostMultiPoly makeBoxMultiPoly(Box box)
{
    BoostMultiPoly mp;
    mp.push_back(makeBoxPoly(box));
    normalizeBoostMultiPoly(mp);
    return mp;
}

// ============================================================
// 9. RectClip - accurate version
// ============================================================
//
// polygon/hole topology를 정확히 유지해야 하면 이 함수 권장.
// Box를 BoostPoly로 만든 뒤 intersection 수행.
//

static BoostMultiPoly rectClipAccurate(
    const BoostMultiPoly& input,
    Box clipBox)
{
    BoostMultiPoly boxMp = makeBoxMultiPoly(clipBox);
    return booleanIntersection(input, boxMp);
}

// ============================================================
// 10. RectClip - fast ring O(n) version
// ============================================================
//
// 단일 BoostRing을 Box로 빠르게 clip한다.
// Sutherland-Hodgman 방식.
//
// 장점:
// - O(n)
// - Ring 단위로 빠름
//
// 단점:
// - polygon + hole topology를 재구성하지 않는다.
// - hole이 box 경계에 걸리면 정확한 polygon 구조로 보장되지 않는다.
// - 정확한 polygon 결과가 필요하면 rectClipAccurate() 사용.
//

enum class ClipEdge
{
    Left,
    Right,
    Bottom,
    Top
};

static bool insideEdge(const point& p, const Box& box, ClipEdge edge)
{
    switch (edge)
    {
    case ClipEdge::Left:
        return p.x >= box.minX;

    case ClipEdge::Right:
        return p.x <= box.maxX;

    case ClipEdge::Bottom:
        return p.y >= box.minY;

    case ClipEdge::Top:
        return p.y <= box.maxY;
    }

    return false;
}

static point intersectEdge(
    const point& a,
    const point& b,
    const Box& box,
    ClipEdge edge)
{
    const long double ax = static_cast<long double>(a.x);
    const long double ay = static_cast<long double>(a.y);
    const long double bx = static_cast<long double>(b.x);
    const long double by = static_cast<long double>(b.y);

    const long double dx = bx - ax;
    const long double dy = by - ay;

    long double t = 0.0L;

    switch (edge)
    {
    case ClipEdge::Left:
        // x = minX
        if (dx == 0.0L)
            return point(box.minX, a.y);

        t = (static_cast<long double>(box.minX) - ax) / dx;
        return point(
            box.minX,
            roundToInt64(ay + t * dy)
        );

    case ClipEdge::Right:
        // x = maxX
        if (dx == 0.0L)
            return point(box.maxX, a.y);

        t = (static_cast<long double>(box.maxX) - ax) / dx;
        return point(
            box.maxX,
            roundToInt64(ay + t * dy)
        );

    case ClipEdge::Bottom:
        // y = minY
        if (dy == 0.0L)
            return point(a.x, box.minY);

        t = (static_cast<long double>(box.minY) - ay) / dy;
        return point(
            roundToInt64(ax + t * dx),
            box.minY
        );

    case ClipEdge::Top:
        // y = maxY
        if (dy == 0.0L)
            return point(a.x, box.maxY);

        t = (static_cast<long double>(box.maxY) - ay) / dy;
        return point(
            roundToInt64(ax + t * dx),
            box.maxY
        );
    }

    return a;
}

static BoostRing clipRingByOneEdge(
    BoostRing input,
    const Box& box,
    ClipEdge edge)
{
    removeDuplicatedLastPoint(input);

    BoostRing output;

    if (input.empty())
        return output;

    point prev = input.back();
    bool prevInside = insideEdge(prev, box, edge);

    for (size_t i = 0; i < input.size(); ++i)
    {
        point curr = input[i];
        bool currInside = insideEdge(curr, box, edge);

        if (prevInside && currInside)
        {
            // inside -> inside
            output.push_back(curr);
        }
        else if (prevInside && !currInside)
        {
            // inside -> outside
            output.push_back(intersectEdge(prev, curr, box, edge));
        }
        else if (!prevInside && currInside)
        {
            // outside -> inside
            output.push_back(intersectEdge(prev, curr, box, edge));
            output.push_back(curr);
        }
        else
        {
            // outside -> outside
        }

        prev = curr;
        prevInside = currInside;
    }

    closeRing(output);
    return output;
}

static BoostRing rectClipRingFast(
    BoostRing ring,
    Box box)
{
    box.normalize();

    ring = clipRingByOneEdge(ring, box, ClipEdge::Left);
    if (ring.size() < 4)
        return BoostRing();

    ring = clipRingByOneEdge(ring, box, ClipEdge::Right);
    if (ring.size() < 4)
        return BoostRing();

    ring = clipRingByOneEdge(ring, box, ClipEdge::Bottom);
    if (ring.size() < 4)
        return BoostRing();

    ring = clipRingByOneEdge(ring, box, ClipEdge::Top);
    if (ring.size() < 4)
        return BoostRing();

    closeRing(ring);
    return ring;
}

static std::vector<StrokeContour> rectClipFlattenContoursFast(
    std::vector<StrokeContour> contours,
    Box box)
{
    box.normalize();

    std::vector<StrokeContour> result;

    for (size_t i = 0; i < contours.size(); ++i)
    {
        StrokeContour c = contours[i];
        c.ring = rectClipRingFast(c.ring, box);

        if (c.ring.size() >= 4)
            result.push_back(c);
    }

    return result;
}

// ============================================================
// 11. Optional: rect clip outer only, no hole topology rebuild
// ============================================================
//
// 이 함수는 BoostPoly의 outer와 inner를 각각 fast clip한다.
// 단, topology 재구성은 하지 않는다.
// 즉, 정확한 polygon 결과가 아니라 contour clipping 용도다.
// 정확한 결과는 rectClipAccurate()를 사용.
//

static std::vector<StrokeContour> rectClipPolyContoursFast(
    const BoostMultiPoly& input,
    Box box)
{
    std::vector<StrokeContour> contours = flattenContoursForStroke(input);
    return rectClipFlattenContoursFast(contours, box);
}

// ============================================================
// 12. Print helpers
// ============================================================

static void printRing(const BoostRing& ring)
{
    std::cout << "Ring size = " << ring.size() << "\n";

    for (size_t i = 0; i < ring.size(); ++i)
    {
        std::cout << "  (" << ring[i].x << ", " << ring[i].y << ")\n";
    }
}

static void printMultiPoly(const BoostMultiPoly& mp, const char* title)
{
    std::cout << "\n=== " << title << " ===\n";
    std::cout << "poly count = " << mp.size() << "\n";

    for (size_t i = 0; i < mp.size(); ++i)
    {
        std::cout << "poly[" << i << "] outer:\n";
        printRing(mp[i].outer());

        for (size_t h = 0; h < mp[i].inners().size(); ++h)
        {
            std::cout << "poly[" << i << "] hole[" << h << "]:\n";
            printRing(mp[i].inners()[h]);
        }
    }
}

static void printStrokePaths(const std::vector<BoostRing>& paths, const char* title)
{
    std::cout << "\n=== " << title << " ===\n";
    std::cout << "path count = " << paths.size() << "\n";

    for (size_t i = 0; i < paths.size(); ++i)
    {
        std::cout << "path[" << i << "]:\n";
        printRing(paths[i]);
    }
}

// ============================================================
// 13. Example
// ============================================================

int main()
{
    // A: 0,0 ~ 100,100 사각형 + 가운데 hole
    BoostPoly polyA;

    polyA.outer().push_back(point(0, 0));
    polyA.outer().push_back(point(100, 0));
    polyA.outer().push_back(point(100, 100));
    polyA.outer().push_back(point(0, 100));
    polyA.outer().push_back(point(0, 0));

    BoostRing holeA0;
    holeA0.push_back(point(20, 20));
    holeA0.push_back(point(35, 20));
    holeA0.push_back(point(35, 35));
    holeA0.push_back(point(20, 35));
    holeA0.push_back(point(20, 20));

    BoostRing holeA1;
    holeA1.push_back(point(65, 65));
    holeA1.push_back(point(80, 65));
    holeA1.push_back(point(80, 80));
    holeA1.push_back(point(65, 80));
    holeA1.push_back(point(65, 65));

    polyA.inners().push_back(holeA0);
    polyA.inners().push_back(holeA1);
    normalizeBoostPoly(polyA);

    BoostMultiPoly A;
    A.push_back(polyA);
    normalizeBoostMultiPoly(A);

    // B: 50,50 ~ 150,150 사각형
    BoostMultiPoly B = makeBoxMultiPoly(Box(50, 50, 150, 150));

    // Boolean 결과
    BoostMultiPoly u = booleanUnion(A, B);
    BoostMultiPoly x = booleanXor(A, B);
    BoostMultiPoly d = booleanDiff(A, B);
    BoostMultiPoly inter = booleanIntersection(A, B);

    printMultiPoly(u, "UNION");
    printMultiPoly(x, "XOR");
    printMultiPoly(d, "DIFF A-B");
    printMultiPoly(inter, "INTERSECTION");

    // Boolean 결과를 hole bridge가 포함된 한붓그리기 path로 변환
    std::vector<BoostRing> unionStrokePaths = makeSingleStrokePaths(u);
    printStrokePaths(unionStrokePaths, "UNION SINGLE-STROKE PATHS");

    // Resize
    BoostMultiPoly grown = resizePoly(A, 5.0, 5.0);
    BoostMultiPoly shrunk = resizePoly(A, -5.0, 5.0);

    printMultiPoly(grown, "RESIZE +5");
    printMultiPoly(shrunk, "RESIZE -5");

    // 기존 방식: outer/hole contour 분리 순회
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

    // RectClip 정확한 버전: Boolean 결과를 먼저 정확히 clip
    Box clipBox(25, 25, 75, 75);

    BoostMultiPoly clippedAccurate = rectClipAccurate(u, clipBox);
    printMultiPoly(clippedAccurate, "UNION RECT CLIP ACCURATE");

    // 목표 흐름: Boolean -> RectClip -> 한붓그리기 path
    std::vector<BoostRing> clippedStrokePaths =
        booleanRectClipSingleStrokePaths(A, B, BoolOp::UnionOp, clipBox);
    printStrokePaths(clippedStrokePaths, "UNION RECTCLIP SINGLE-STROKE PATHS");

    // RectClip 빠른 contour 버전
    std::vector<StrokeContour> clippedContours =
        rectClipPolyContoursFast(A, clipBox);

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
