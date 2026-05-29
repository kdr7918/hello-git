// boost_geometry_custom_boolean_rectclip.cpp
// C++11 예시
//
// g++ -std=c++11 boost_geometry_custom_boolean_rectclip.cpp -o test
//
// 목적:
// 1. CustomPoly / CustomMultiPoly <-> Boost.Geometry 변환
// 2. Boolean: OR, XOR, DIFF, AND
// 3. Resize: Boost.Geometry buffer 사용
// 4. Hole 포함 polygon을 contour 단위로 순회하는 "한붓그리기용 flatten"
// 5. RectClip:
//    - 정확한 polygon/hole 보존: Boost intersection(box)
//    - 빠른 ring 단위 O(n) clipping: Sutherland-Hodgman
//
// 주의:
// - Boost.Geometry는 polygon outer / hole을 "진짜 하나의 폐곡선"으로 자동 연결해주지 않는다.
// - EDA에서 outer + hole을 한 번에 stroke/path로 처리하고 싶으면,
//   보통 outer contour와 hole contour를 따로 순회하거나,
//   별도 bridge-cut 알고리즘으로 hole을 outer에 연결해야 한다.
// - 아래 flattenContours()는 outer와 hole을 contour 단위로 뽑아주는 방식이다.

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/geometries/box.hpp>

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace bg = boost::geometry;

// ============================================================
// 1. Custom geometry type
// ============================================================

struct CustomPoint
{
    double x;
    double y;

    CustomPoint() : x(0), y(0) {}
    CustomPoint(double x_, double y_) : x(x_), y(y_) {}
};

typedef std::vector<CustomPoint> CustomRing;

struct CustomPoly
{
    // outer: polygon 외곽선
    CustomRing outer;

    // holes: polygon 내부 hole들
    std::vector<CustomRing> holes;
};

typedef std::vector<CustomPoly> CustomMultiPoly;

struct CustomRect
{
    double minX;
    double minY;
    double maxX;
    double maxY;

    CustomRect() : minX(0), minY(0), maxX(0), maxY(0) {}
    CustomRect(double x1, double y1, double x2, double y2)
        : minX(x1), minY(y1), maxX(x2), maxY(y2) {}
};

// ============================================================
// 2. Boost.Geometry type
// ============================================================

// false = CCW orientation
// true  = closed ring
//
// bg::correct()를 호출하면 outer/hole orientation, closure를 Boost 규칙에 맞게 보정한다.
typedef bg::model::d2::point_xy<double> BoostPoint;
typedef bg::model::polygon<BoostPoint, false, true> BoostPoly;
typedef bg::model::multi_polygon<BoostPoly> BoostMultiPoly;
typedef bg::model::box<BoostPoint> BoostBox;

// ============================================================
// 3. Utility
// ============================================================

static bool samePoint(const CustomPoint& a, const CustomPoint& b, double eps = 1e-9)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

static void closeRing(CustomRing& ring)
{
    if (ring.empty())
        return;

    if (!samePoint(ring.front(), ring.back()))
        ring.push_back(ring.front());
}

static void removeDuplicatedLastPoint(CustomRing& ring)
{
    if (ring.size() >= 2 && samePoint(ring.front(), ring.back()))
        ring.pop_back();
}

// signed area
// area > 0 : CCW
// area < 0 : CW
static double signedArea(const CustomRing& ring)
{
    if (ring.size() < 3)
        return 0.0;

    double a = 0.0;
    const size_t n = ring.size();

    for (size_t i = 0; i < n; ++i)
    {
        const CustomPoint& p = ring[i];
        const CustomPoint& q = ring[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }

    return a * 0.5;
}

static void reverseRing(CustomRing& ring)
{
    removeDuplicatedLastPoint(ring);
    std::reverse(ring.begin(), ring.end());
    closeRing(ring);
}

// Boost polygon<..., false, true>는 outer CCW, inner CW가 자연스럽다.
// bg::correct()를 쓰므로 수동 orientation 보정은 필수는 아니지만,
// custom 쪽에서도 정리하고 싶으면 사용한다.
static void normalizeCustomPoly(CustomPoly& poly)
{
    closeRing(poly.outer);

    // outer는 CCW 권장
    if (signedArea(poly.outer) < 0)
        reverseRing(poly.outer);

    for (size_t i = 0; i < poly.holes.size(); ++i)
    {
        closeRing(poly.holes[i]);

        // hole은 CW 권장
        if (signedArea(poly.holes[i]) > 0)
            reverseRing(poly.holes[i]);
    }
}

// ============================================================
// 4. Custom <-> Boost conversion
// ============================================================

static BoostPoint toBoostPoint(const CustomPoint& p)
{
    return BoostPoint(p.x, p.y);
}

static CustomPoint toCustomPoint(const BoostPoint& p)
{
    return CustomPoint(bg::get<0>(p), bg::get<1>(p));
}

static BoostPoly toBoostPoly(CustomPoly poly)
{
    normalizeCustomPoly(poly);

    BoostPoly bp;

    for (size_t i = 0; i < poly.outer.size(); ++i)
    {
        bg::append(bp.outer(), toBoostPoint(poly.outer[i]));
    }

    bp.inners().resize(poly.holes.size());

    for (size_t h = 0; h < poly.holes.size(); ++h)
    {
        for (size_t i = 0; i < poly.holes[h].size(); ++i)
        {
            bg::append(bp.inners()[h], toBoostPoint(poly.holes[h][i]));
        }
    }

    // orientation, closed 여부 등을 Boost 기준으로 보정
    bg::correct(bp);

    return bp;
}

static BoostMultiPoly toBoostMultiPoly(const CustomMultiPoly& mp)
{
    BoostMultiPoly out;

    for (size_t i = 0; i < mp.size(); ++i)
    {
        out.push_back(toBoostPoly(mp[i]));
    }

    bg::correct(out);
    return out;
}

static CustomRing toCustomRing(const BoostPoly::ring_type& ring)
{
    CustomRing out;

    for (size_t i = 0; i < ring.size(); ++i)
    {
        out.push_back(toCustomPoint(ring[i]));
    }

    closeRing(out);
    return out;
}

static CustomPoly toCustomPoly(const BoostPoly& bp)
{
    CustomPoly out;

    out.outer = toCustomRing(bp.outer());

    for (size_t h = 0; h < bp.inners().size(); ++h)
    {
        out.holes.push_back(toCustomRing(bp.inners()[h]));
    }

    normalizeCustomPoly(out);
    return out;
}

static CustomMultiPoly toCustomMultiPoly(const BoostMultiPoly& bmp)
{
    CustomMultiPoly out;

    for (size_t i = 0; i < bmp.size(); ++i)
    {
        out.push_back(toCustomPoly(bmp[i]));
    }

    return out;
}

// ============================================================
// 5. Boolean operations
// ============================================================

enum class BoolOp
{
    UnionOp,
    XorOp,
    DifferenceOp,
    IntersectionOp
};

static CustomMultiPoly booleanOp(
    const CustomMultiPoly& a,
    const CustomMultiPoly& b,
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
    return toCustomMultiPoly(result);
}

static CustomMultiPoly booleanUnion(
    const CustomMultiPoly& a,
    const CustomMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::UnionOp);
}

static CustomMultiPoly booleanXor(
    const CustomMultiPoly& a,
    const CustomMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::XorOp);
}

static CustomMultiPoly booleanDiff(
    const CustomMultiPoly& a,
    const CustomMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::DifferenceOp);
}

static CustomMultiPoly booleanIntersection(
    const CustomMultiPoly& a,
    const CustomMultiPoly& b)
{
    return booleanOp(a, b, BoolOp::IntersectionOp);
}

// ============================================================
// 6. Resize / Offset
// ============================================================
//
// delta > 0 : 확장
// delta < 0 : 축소
//
// miterLimit:
// - 직각 유지에 중요하다.
// - 너무 작으면 모서리가 bevel처럼 잘릴 수 있다.
// - EDA 도형이면 2.0 ~ 5.0 정도부터 테스트 권장.
// - delta가 아주 작을 때도 수치오차/짧은 edge 때문에 예상과 다를 수 있다.
//

static CustomMultiPoly resizePoly(
    const CustomMultiPoly& input,
    double delta,
    double miterLimit = 5.0)
{
    BoostMultiPoly bin = toBoostMultiPoly(input);
    BoostMultiPoly bout;

    namespace bs = boost::geometry::strategy::buffer;

    bs::distance_symmetric<double> distanceStrategy(delta);
    bs::side_straight sideStrategy;
    bs::join_miter joinStrategy(miterLimit);

    // polygon buffer에서는 end strategy는 사실상 큰 의미가 없다.
    bs::end_flat endStrategy;

    // polygon 꼭짓점 처리용
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
    return toCustomMultiPoly(bout);
}

// ============================================================
// 7. Hole 포함 "한붓그리기용" contour flatten
// ============================================================
//
// Boost.Geometry 자체는 outer와 hole을 하나의 단일 path로 연결하지 않는다.
// 아래 함수는 polygon을 다음 형태로 펼친다.
//
// result:
//   poly0 outer
//   poly0 hole0
//   poly0 hole1
//   poly1 outer
//   poly1 hole0
//   ...
//
// 즉, drawing/stroking/export 단계에서 contour 단위로 순회하기 좋다.
//
// 진짜 의미의 "한붓그리기", 즉 outer와 hole을 bridge edge로 연결해서
// 하나의 closed contour로 만드는 것은 별도 bridge-cut 알고리즘이 필요하다.
//

struct StrokeContour
{
    CustomRing ring;
    bool isHole;
    size_t polyIndex;
    size_t holeIndex; // isHole == false면 0
};

static std::vector<StrokeContour> flattenContoursForStroke(
    const CustomMultiPoly& mp)
{
    std::vector<StrokeContour> out;

    for (size_t p = 0; p < mp.size(); ++p)
    {
        CustomPoly poly = mp[p];
        normalizeCustomPoly(poly);

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
// 8. RectClip - 정확한 버전
// ============================================================
//
// polygon/hole 구조를 정확하게 유지하려면 Boost intersection(box)을 쓰는 게 안전하다.
// 내부적으로는 Boost가 topology를 다시 구성한다.
//

static CustomMultiPoly rectClipAccurate(
    const CustomMultiPoly& input,
    const CustomRect& r)
{
    BoostMultiPoly bin = toBoostMultiPoly(input);
    BoostBox box(
        BoostPoint(r.minX, r.minY),
        BoostPoint(r.maxX, r.maxY)
    );

    BoostMultiPoly bout;

    bg::intersection(bin, box, bout);
    bg::correct(bout);

    return toCustomMultiPoly(bout);
}

// ============================================================
// 9. RectClip - 빠른 ring 단위 O(n) Sutherland-Hodgman
// ============================================================
//
// 이 함수는 "하나의 contour ring"을 사각형으로 clip한다.
// hole topology를 자동 재구성하지 않는다.
// 즉, 단일 ring path를 빠르게 자를 때 사용.
//
// hole까지 포함된 polygon 전체를 정확하게 자르려면 rectClipAccurate() 사용 권장.
//

enum class ClipEdge
{
    Left,
    Right,
    Bottom,
    Top
};

static bool insideEdge(const CustomPoint& p, const CustomRect& r, ClipEdge e)
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

static CustomPoint intersectEdge(
    const CustomPoint& a,
    const CustomPoint& b,
    const CustomRect& r,
    ClipEdge e)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;

    double t = 0.0;

    switch (e)
    {
    case ClipEdge::Left:
        // x = minX
        if (std::fabs(dx) < 1e-30)
            return CustomPoint(r.minX, a.y);
        t = (r.minX - a.x) / dx;
        return CustomPoint(r.minX, a.y + t * dy);

    case ClipEdge::Right:
        // x = maxX
        if (std::fabs(dx) < 1e-30)
            return CustomPoint(r.maxX, a.y);
        t = (r.maxX - a.x) / dx;
        return CustomPoint(r.maxX, a.y + t * dy);

    case ClipEdge::Bottom:
        // y = minY
        if (std::fabs(dy) < 1e-30)
            return CustomPoint(a.x, r.minY);
        t = (r.minY - a.y) / dy;
        return CustomPoint(a.x + t * dx, r.minY);

    case ClipEdge::Top:
        // y = maxY
        if (std::fabs(dy) < 1e-30)
            return CustomPoint(a.x, r.maxY);
        t = (r.maxY - a.y) / dy;
        return CustomPoint(a.x + t * dx, r.maxY);
    }

    return a;
}

static CustomRing clipRingByOneEdge(
    const CustomRing& inputRing,
    const CustomRect& r,
    ClipEdge edge)
{
    CustomRing in = inputRing;
    removeDuplicatedLastPoint(in);

    CustomRing out;

    if (in.empty())
        return out;

    CustomPoint prev = in.back();
    bool prevInside = insideEdge(prev, r, edge);

    for (size_t i = 0; i < in.size(); ++i)
    {
        CustomPoint curr = in[i];
        bool currInside = insideEdge(curr, r, edge);

        if (prevInside && currInside)
        {
            // inside -> inside
            out.push_back(curr);
        }
        else if (prevInside && !currInside)
        {
            // inside -> outside
            out.push_back(intersectEdge(prev, curr, r, edge));
        }
        else if (!prevInside && currInside)
        {
            // outside -> inside
            out.push_back(intersectEdge(prev, curr, r, edge));
            out.push_back(curr);
        }
        else
        {
            // outside -> outside
            // 아무것도 추가하지 않음
        }

        prev = curr;
        prevInside = currInside;
    }

    closeRing(out);
    return out;
}

static CustomRing rectClipRingFast(
    const CustomRing& ring,
    const CustomRect& r)
{
    CustomRing out = ring;

    out = clipRingByOneEdge(out, r, ClipEdge::Left);
    if (out.size() < 4)
        return CustomRing();

    out = clipRingByOneEdge(out, r, ClipEdge::Right);
    if (out.size() < 4)
        return CustomRing();

    out = clipRingByOneEdge(out, r, ClipEdge::Bottom);
    if (out.size() < 4)
        return CustomRing();

    out = clipRingByOneEdge(out, r, ClipEdge::Top);
    if (out.size() < 4)
        return CustomRing();

    closeRing(out);
    return out;
}

// contour flatten 결과에 대해 빠르게 rect clip
// topology 재구성은 하지 않는다.
static std::vector<StrokeContour> rectClipFlattenContoursFast(
    const std::vector<StrokeContour>& contours,
    const CustomRect& r)
{
    std::vector<StrokeContour> out;

    for (size_t i = 0; i < contours.size(); ++i)
    {
        StrokeContour c = contours[i];
        c.ring = rectClipRingFast(c.ring, r);

        if (c.ring.size() >= 4)
            out.push_back(c);
    }

    return out;
}

// ============================================================
// 10. 예시 출력용
// ============================================================

static void printRing(const CustomRing& ring)
{
    std::cout << "Ring size = " << ring.size() << "\n";

    for (size_t i = 0; i < ring.size(); ++i)
    {
        std::cout << "  (" << ring[i].x << ", " << ring[i].y << ")\n";
    }
}

static void printMultiPoly(const CustomMultiPoly& mp, const char* title)
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
// 11. 사용 예시
// ============================================================

int main()
{
    // ------------------------------------------------------------
    // A: 0,0 ~ 100,100 사각형 + 가운데 hole
    // ------------------------------------------------------------
    CustomPoly polyA;

    polyA.outer.push_back(CustomPoint(0, 0));
    polyA.outer.push_back(CustomPoint(100, 0));
    polyA.outer.push_back(CustomPoint(100, 100));
    polyA.outer.push_back(CustomPoint(0, 100));
    closeRing(polyA.outer);

    CustomRing holeA;
    holeA.push_back(CustomPoint(40, 40));
    holeA.push_back(CustomPoint(60, 40));
    holeA.push_back(CustomPoint(60, 60));
    holeA.push_back(CustomPoint(40, 60));
    closeRing(holeA);

    polyA.holes.push_back(holeA);

    CustomMultiPoly A;
    A.push_back(polyA);

    // ------------------------------------------------------------
    // B: 50,50 ~ 150,150 사각형
    // ------------------------------------------------------------
    CustomPoly polyB;

    polyB.outer.push_back(CustomPoint(50, 50));
    polyB.outer.push_back(CustomPoint(150, 50));
    polyB.outer.push_back(CustomPoint(150, 150));
    polyB.outer.push_back(CustomPoint(50, 150));
    closeRing(polyB.outer);

    CustomMultiPoly B;
    B.push_back(polyB);

    // ------------------------------------------------------------
    // Boolean examples
    // ------------------------------------------------------------
    CustomMultiPoly u = booleanUnion(A, B);
    CustomMultiPoly x = booleanXor(A, B);
    CustomMultiPoly d = booleanDiff(A, B);
    CustomMultiPoly i = booleanIntersection(A, B);

    printMultiPoly(u, "UNION");
    printMultiPoly(x, "XOR");
    printMultiPoly(d, "DIFF A-B");
    printMultiPoly(i, "INTERSECTION");

    // ------------------------------------------------------------
    // Resize / Offset
    // ------------------------------------------------------------
    CustomMultiPoly grown = resizePoly(A, 5.0, 5.0);
    CustomMultiPoly shrunk = resizePoly(A, -5.0, 5.0);

    printMultiPoly(grown, "RESIZE +5");
    printMultiPoly(shrunk, "RESIZE -5");

    // ------------------------------------------------------------
    // Hole 포함 contour flatten
    // ------------------------------------------------------------
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

    // ------------------------------------------------------------
    // RectClip 정확한 버전
    // ------------------------------------------------------------
    CustomRect clipRect(25, 25, 75, 75);

    CustomMultiPoly clippedAccurate = rectClipAccurate(A, clipRect);
    printMultiPoly(clippedAccurate, "RECT CLIP ACCURATE");

    // ------------------------------------------------------------
    // RectClip 빠른 contour 단위 버전
    // ------------------------------------------------------------
    std::vector<StrokeContour> clippedContours =
        rectClipFlattenContoursFast(contours, clipRect);

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
