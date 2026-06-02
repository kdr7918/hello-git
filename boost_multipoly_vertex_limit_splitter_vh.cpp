// BoostMultiPoly vertex limit splitter by vertical/horizontal box cut
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
// - BoostMultiPoly를 수직/수평 Box intersection으로 분할
// - 각 결과 BoostPoly의 outer + holes 총 정점 수가 maxVertexCount 이하가 되도록 반복
// - Fan split / Chunk split보다 polygon 면적 의미를 훨씬 잘 보존
//
// 주의:
// - Boost.Geometry에는 unary dissolve/merge가 없으므로 입력이 겹친 multi polygon이면
//   먼저 mergeSelf() 같은 누적 union으로 정리한 뒤 이 함수를 쓰는 것을 권장.
// - 너무 복잡한 도형은 maxDepth 제한까지 가도 vertex limit 이하가 안 될 수 있음.
// - split line이 정점/edge와 정확히 겹치면 Boost overlay가 예민할 수 있으므로,
//   필요하면 split 좌표를 1 DBU 정도 shift하는 전략을 추가할 수 있음.

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/ring.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
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

static BoostMultiPoly rectClipAccurate(
    const BoostMultiPoly& input,
    Box clipBox)
{
    BoostMultiPoly in = input;
    bg::correct(in);

    BoostMultiPoly boxMp = makeBoxMultiPoly(clipBox);

    BoostMultiPoly out;
    bg::intersection(in, boxMp, out);
    bg::correct(out);

    return out;
}

static Box getEnvelopeBox(const BoostMultiPoly& mp)
{
    bg::model::box<point> b;
    bg::envelope(mp, b);

    Box out;
    out.minX = bg::get<bg::min_corner, 0>(b);
    out.minY = bg::get<bg::min_corner, 1>(b);
    out.maxX = bg::get<bg::max_corner, 0>(b);
    out.maxY = bg::get<bg::max_corner, 1>(b);

    normalizeBox(out);
    return out;
}

static size_t ringVertexCount(const BoostRing& ring)
{
    if (ring.empty())
        return 0;

    // closed ring이면 마지막 중복점을 제외하고 카운트하고 싶으면 -1
    if (ring.size() >= 2 && samePoint(ring.front(), ring.back()))
        return ring.size() - 1;

    return ring.size();
}

static size_t polyVertexCount(const BoostPoly& poly)
{
    size_t count = 0;

    count += ringVertexCount(poly.outer());

    for (size_t i = 0; i < poly.inners().size(); ++i)
        count += ringVertexCount(poly.inners()[i]);

    return count;
}

static size_t multiPolyMaxPolyVertexCount(const BoostMultiPoly& mp)
{
    size_t maxCount = 0;

    for (size_t i = 0; i < mp.size(); ++i)
        maxCount = std::max(maxCount, polyVertexCount(mp[i]));

    return maxCount;
}

static bool isWithinVertexLimit(
    const BoostMultiPoly& mp,
    size_t maxVertexCount)
{
    for (size_t i = 0; i < mp.size(); ++i)
    {
        if (polyVertexCount(mp[i]) > maxVertexCount)
            return false;
    }

    return true;
}

static bool isEmptyGeometry(const BoostMultiPoly& mp)
{
    return mp.empty();
}

static void appendNonEmpty(
    std::vector<BoostMultiPoly>& out,
    const BoostMultiPoly& mp)
{
    if (!isEmptyGeometry(mp))
        out.push_back(mp);
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
    const BoostMultiPoly& input,
    size_t maxVertexCount,
    int depth,
    int maxDepth,
    std::vector<BoostMultiPoly>& out)
{
    if (input.empty())
        return;

    BoostMultiPoly mp = input;
    bg::correct(mp);

    if (isWithinVertexLimit(mp, maxVertexCount))
    {
        out.push_back(mp);
        return;
    }

    if (depth >= maxDepth)
    {
        // 더 이상 자르지 않고 반환.
        // 이 경우 일부 polygon은 maxVertexCount를 초과할 수 있다.
        out.push_back(mp);
        return;
    }

    Box env = getEnvelopeBox(mp);

    if (!canSplitBox(env))
    {
        out.push_back(mp);
        return;
    }

    Box boxA;
    Box boxB;

    if (!splitBoxByLongAxis(env, boxA, boxB))
    {
        out.push_back(mp);
        return;
    }

    BoostMultiPoly partA = rectClipAccurate(mp, boxA);
    BoostMultiPoly partB = rectClipAccurate(mp, boxB);

    // 분할했는데 한쪽이 비거나, 둘 다 원본과 거의 같은 식으로 안 잘리면 무한 루프 방지
    if (partA.empty() && partB.empty())
        return;

    if (!partA.empty())
        splitByVertexLimitRecursive(partA, maxVertexCount, depth + 1, maxDepth, out);

    if (!partB.empty())
        splitByVertexLimitRecursive(partB, maxVertexCount, depth + 1, maxDepth, out);
}

// 메인 함수.
// 결과는 여러 BoostMultiPoly 조각으로 반환.
// 각 조각은 box intersection 결과라 polygon topology가 유지된다.
static std::vector<BoostMultiPoly> splitBoostMultiPolyByVertexLimitVH(
    BoostMultiPoly input,
    size_t maxVertexCount,
    int maxDepth = 32)
{
    std::vector<BoostMultiPoly> out;

    if (maxVertexCount < 4)
        maxVertexCount = 4;

    bg::correct(input);

    splitByVertexLimitRecursive(
        input,
        maxVertexCount,
        0,
        maxDepth,
        out);

    return out;
}

// 결과를 하나의 BoostMultiPoly로 합쳐 받고 싶을 때.
// 단, 조각들이 서로 edge를 공유할 수 있으므로,
// 다시 union하면 원래대로 merge될 수 있다.
// export용이면 vector<BoostMultiPoly> 그대로 쓰는 것을 권장.
static BoostMultiPoly flattenSplitResult(
    const std::vector<BoostMultiPoly>& pieces)
{
    BoostMultiPoly out;

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        for (size_t j = 0; j < pieces[i].size(); ++j)
            out.push_back(pieces[i][j]);
    }

    bg::correct(out);
    return out;
}

// 사용 예:
//
// BoostMultiPoly input = ...;
//
// size_t limit = 200;
//
// std::vector<BoostMultiPoly> pieces =
//     splitBoostMultiPolyByVertexLimitVH(input, limit, 32);
//
// for (size_t i = 0; i < pieces.size(); ++i)
// {
//     // pieces[i]를 각각 export / 처리
// }
//
// BoostMultiPoly flat = flattenSplitResult(pieces);
