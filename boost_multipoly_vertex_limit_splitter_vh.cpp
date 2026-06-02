// BoostRing vertex limit splitter by vertical/horizontal box cut
// C++11
//
// 전제:
// struct point { int64_t x; int64_t y; };
// struct Box {
//     int64_t minX, minY, maxX, maxY;
//     void normalize();
// };
//
// typedef boost::geometry::model::ring<point, false, true> BoostRing;
// typedef boost::geometry::model::polygon<point, false, true> BoostPoly;
// typedef boost::geometry::model::multi_polygon<BoostPoly> BoostMultiPoly;
//
// 목적:
// - 단일 BoostRing을 수직/수평 Box intersection으로 분할
// - 각 결과 BoostRing의 정점 수가 maxVertexCount 이하가 되도록 반복
// - 결과 타입은 std::vector<BoostRing>
//
// 주의:
// - 입력 ring은 simple closed contour라고 가정한다.
// - hole이 있는 polygon은 outer/hole을 BoostRing으로 flatten하기 전에 polygon 단위로
//   처리해야 topology 손실이 없다.
// - split line이 정점/edge와 정확히 겹치면 Boost overlay가 예민할 수 있으므로,
//   필요하면 split 좌표를 1 DBU 정도 shift하는 전략을 추가할 수 있음.

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/ring.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace bg = boost::geometry;

// 이미 프로젝트에서 등록했다면 제거
// BOOST_GEOMETRY_REGISTER_POINT_2D(point, int64_t, bg::cs::cartesian, x, y)

static bool samePoint(const point& a, const point& b)
{
    return a.x == b.x && a.y == b.y;
}

static void closeRing(BoostRing& ring)
{
    if (!ring.empty() && !samePoint(ring.front(), ring.back()))
        ring.push_back(ring.front());
}

static void normalizeBox(Box& b)
{
    if (b.minX > b.maxX)
        std::swap(b.minX, b.maxX);

    if (b.minY > b.maxY)
        std::swap(b.minY, b.maxY);
}

static size_t ringVertexCount(const BoostRing& ring)
{
    if (ring.empty())
        return 0;

    // closed ring이면 마지막 중복점은 제외하고 카운트한다.
    if (ring.size() >= 2 && samePoint(ring.front(), ring.back()))
        return ring.size() - 1;

    return ring.size();
}

static bool isValidOutputRing(const BoostRing& ring)
{
    return ringVertexCount(ring) >= 3;
}

static BoostPoly makePolyFromRing(BoostRing ring)
{
    closeRing(ring);

    BoostPoly p;
    p.outer() = ring;

    bg::correct(p);
    return p;
}

static BoostMultiPoly makeMultiPolyFromRing(BoostRing ring)
{
    BoostMultiPoly mp;
    mp.push_back(makePolyFromRing(ring));
    bg::correct(mp);
    return mp;
}

static BoostPoly makeBoxPoly(Box box)
{
    normalizeBox(box);

    BoostPoly p;

    p.outer().push_back(point{box.minX, box.minY});
    p.outer().push_back(point{box.maxX, box.minY});
    p.outer().push_back(point{box.maxX, box.maxY});
    p.outer().push_back(point{box.minX, box.maxY});
    p.outer().push_back(point{box.minX, box.minY});

    bg::correct(p);
    return p;
}

static BoostMultiPoly makeBoxMultiPoly(Box box)
{
    BoostMultiPoly mp;
    mp.push_back(makeBoxPoly(box));
    bg::correct(mp);
    return mp;
}

static void appendOuterRings(
    std::vector<BoostRing>& out,
    const BoostMultiPoly& mp)
{
    for (size_t i = 0; i < mp.size(); ++i)
    {
        BoostRing ring = mp[i].outer();
        closeRing(ring);

        if (isValidOutputRing(ring))
            out.push_back(ring);
    }
}

static std::vector<BoostRing> rectClipRingAccurate(
    const BoostRing& input,
    Box clipBox)
{
    std::vector<BoostRing> rings;

    if (!isValidOutputRing(input))
        return rings;

    BoostMultiPoly in = makeMultiPolyFromRing(input);
    BoostMultiPoly boxMp = makeBoxMultiPoly(clipBox);

    BoostMultiPoly clipped;
    bg::intersection(in, boxMp, clipped);
    bg::correct(clipped);

    appendOuterRings(rings, clipped);
    return rings;
}

static Box getEnvelopeBox(const BoostRing& ring)
{
    bg::model::box<point> b;
    bg::envelope(ring, b);

    Box out;
    out.minX = bg::get<bg::min_corner, 0>(b);
    out.minY = bg::get<bg::min_corner, 1>(b);
    out.maxX = bg::get<bg::max_corner, 0>(b);
    out.maxY = bg::get<bg::max_corner, 1>(b);

    normalizeBox(out);
    return out;
}

static bool isWithinVertexLimit(
    const BoostRing& ring,
    size_t maxVertexCount)
{
    return ringVertexCount(ring) <= maxVertexCount;
}

static bool canSplitBox(const Box& box)
{
    return box.maxX > box.minX || box.maxY > box.minY;
}

// bbox의 긴 방향 기준으로 2분할.
// width >= height면 수직선으로 좌우 분할.
// height > width면 수평선으로 상하 분할.
static bool splitBoxByLongAxis(
    const Box& box,
    Box& a,
    Box& b)
{
    Box src = box;
    normalizeBox(src);

    const int64_t width = src.maxX - src.minX;
    const int64_t height = src.maxY - src.minY;

    if (width <= 0 && height <= 0)
        return false;

    if (width >= height)
    {
        if (width <= 0)
            return false;

        const int64_t midX = src.minX + width / 2;

        // mid가 경계와 같으면 분할 불가
        if (midX <= src.minX || midX >= src.maxX)
            return false;

        // left box
        a.minX = src.minX;
        a.minY = src.minY;
        a.maxX = midX;
        a.maxY = src.maxY;

        // right box
        b.minX = midX;
        b.minY = src.minY;
        b.maxX = src.maxX;
        b.maxY = src.maxY;
    }
    else
    {
        if (height <= 0)
            return false;

        const int64_t midY = src.minY + height / 2;

        // mid가 경계와 같으면 분할 불가
        if (midY <= src.minY || midY >= src.maxY)
            return false;

        // bottom box
        a.minX = src.minX;
        a.minY = src.minY;
        a.maxX = src.maxX;
        a.maxY = midY;

        // top box
        b.minX = src.minX;
        b.minY = midY;
        b.maxX = src.maxX;
        b.maxY = src.maxY;
    }

    normalizeBox(a);
    normalizeBox(b);
    return true;
}

static void splitByVertexLimitRecursive(
    const BoostRing& input,
    size_t maxVertexCount,
    int depth,
    int maxDepth,
    std::vector<BoostRing>& out)
{
    if (!isValidOutputRing(input))
        return;

    BoostRing ring = input;
    closeRing(ring);
    bg::correct(ring);

    if (isWithinVertexLimit(ring, maxVertexCount))
    {
        out.push_back(ring);
        return;
    }

    if (depth >= maxDepth)
    {
        // 더 이상 자르지 않고 반환.
        // 이 경우 일부 ring은 maxVertexCount를 초과할 수 있다.
        out.push_back(ring);
        return;
    }

    Box env = getEnvelopeBox(ring);

    if (!canSplitBox(env))
    {
        out.push_back(ring);
        return;
    }

    Box boxA;
    Box boxB;

    if (!splitBoxByLongAxis(env, boxA, boxB))
    {
        out.push_back(ring);
        return;
    }

    std::vector<BoostRing> partA = rectClipRingAccurate(ring, boxA);
    std::vector<BoostRing> partB = rectClipRingAccurate(ring, boxB);

    // overlay 실패나 퇴화 케이스에서 무한 재귀를 피한다.
    if (partA.empty() && partB.empty())
        return;

    for (size_t i = 0; i < partA.size(); ++i)
        splitByVertexLimitRecursive(partA[i], maxVertexCount, depth + 1, maxDepth, out);

    for (size_t i = 0; i < partB.size(); ++i)
        splitByVertexLimitRecursive(partB[i], maxVertexCount, depth + 1, maxDepth, out);
}

// 메인 함수.
// 입력 BoostRing을 box intersection으로 반복 분할한다.
// maxVertexCount는 closed ring의 마지막 중복점을 제외한 unique vertex 기준이다.
static std::vector<BoostRing> splitBoostRingByVertexLimitVH(
    BoostRing input,
    size_t maxVertexCount,
    int maxDepth = 32)
{
    std::vector<BoostRing> out;

    if (maxVertexCount < 3)
        maxVertexCount = 3;

    closeRing(input);
    bg::correct(input);

    splitByVertexLimitRecursive(
        input,
        maxVertexCount,
        0,
        maxDepth,
        out);

    return out;
}

// 사용 예:
//
// BoostRing input = ...;
//
// size_t limit = 200;
//
// std::vector<BoostRing> pieces =
//     splitBoostRingByVertexLimitVH(input, limit, 32);
//
// for (size_t i = 0; i < pieces.size(); ++i)
// {
//     // pieces[i]를 각각 export / 처리
// }
