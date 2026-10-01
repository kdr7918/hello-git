// OasisCreator standalone example; requires Multigon OASIS headers and libraries.
// Build against Multigon: g++ -std=c++11 -O2 -I/path/to/multigon/src
//   oasis-repetition-million-gen.cc /path/to/multigon/lib/liboasis.a
//   /path/to/multigon/lib/libmisc.a -lz -o oasis-repetition-million-gen
// Run: ./oasis-repetition-million-gen OUTPUT_DIRECTORY [--side N]
// Default N=1000: 11 repetition encodings plus one explicit million-element file.
// OUTPUT_DIRECTORY must exist. N=2..1000 is useful for small verification runs.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

#include "oasis/creator.h"
#include "oasis/names.h"
#include "oasis/oasis.h"

namespace {
const long Pitch = 32768;  // OASIS grid units; larger than polygon diameter
const long Radius = 10000;
const unsigned Vertices = 1000;

Oasis::PointList polygon1000()
{
    Oasis::PointList points;
    points.init(Oasis::PointList::AllAngle);
    const double pi = std::acos(-1.0);
    // The first vertex is implicit in OASIS's point-list, so it is (0,0)
    // relative to the element anchor at (Radius,0).  No repeated closing point.
    for (unsigned i = 0; i < Vertices; ++i) {
        const double angle = 2.0 * pi * i / Vertices;
        points.addPoint(Oasis::Delta(
            static_cast<long>(std::lround(Radius * std::cos(angle))) - Radius,
            static_cast<long>(std::lround(Radius * std::sin(angle)))));
    }
    return points;
}

void makeRep(Oasis::Repetition& rep, int type, unsigned side)
{
    using Oasis::Delta;
    switch (type) {
    case 1: rep.makeMatrix(side, side, Pitch, Pitch); break;
    case 2: rep.makeUniformX(side, Pitch); break;
    case 3: rep.makeUniformY(side, Pitch); break;
    case 4: case 5:
        if (type == 4) rep.makeVaryingX(side);
        else rep.makeGridVaryingX(side, Pitch);
        for (unsigned i = 0; i < side; ++i)
            rep.addVaryingXoffset(static_cast<long>(i) * Pitch);
        break;
    case 6: case 7:
        if (type == 6) rep.makeVaryingY(side);
        else rep.makeGridVaryingY(side, Pitch);
        for (unsigned i = 0; i < side; ++i)
            rep.addVaryingYoffset(static_cast<long>(i) * Pitch);
        break;
    case 8:
        rep.makeTiltedMatrix(side, side, Delta(Pitch, 0), Delta(0, Pitch));
        break; // same square grid, encoded as a tilted-matrix repetition
    case 9:
        rep.makeDiagonal(side, Delta(Pitch, Pitch));
        break; // diagonal rows form a parallelogram instead of a square
    case 10: case 11:
        if (type == 10) rep.makeArbitrary(side * side);
        else rep.makeGridArbitrary(side * side, Pitch);
        for (unsigned row = 0; row < side; ++row)
            for (unsigned col = 0; col < side; ++col)
                rep.addDelta(Delta(static_cast<long>(col) * Pitch,
                                   static_cast<long>(row) * Pitch));
        break;
    default: throw std::invalid_argument("unsupported repetition type");
    }
}

void writeLayout(const std::string& filename, int type, unsigned side,
                 const Oasis::PointList& polygon)
{
    Oasis::OasisCreatorOptions options(false);
    Oasis::OasisCreator creator(filename.c_str(), options);
    // Disabling CBLOCK makes record inspection straightforward and avoids
    // spending CPU compressing a million explicit elements.
    creator.setCompression(false);
    creator.beginFile("1.0", Oasis::Oreal(1000), Oasis::Validation::Checksum32);
    Oasis::CellName cell("TOP");  // must outlive endFile()
    creator.registerCellName(&cell);
    creator.beginCell(&cell);

    Oasis::Repetition rep;
    if (type != 0) makeRep(rep, type, side);
    if (type == 1 || type == 8 || type == 10 || type == 11) {
        creator.beginPolygon(1, 0, Radius, 0, polygon, &rep);
    } else {
        for (unsigned row = 0; row < side; ++row) {
            for (unsigned col = 0; col < (type == 0 ? side : 1u); ++col) {
                // Y-family repetitions extend along Y: put each base in a
                // different column. X-family bases occupy different rows.
                const bool vertical = (type == 3 || type == 6 || type == 7);
                const long x = static_cast<long>(vertical ? row : col) * Pitch + Radius;
                // Type 9 advances along both axes and forms a parallelogram.
                const long y = static_cast<long>(vertical ? 0 : row) * Pitch;
                creator.beginPolygon(1, 0, x, y, polygon,
                                     type == 0 ? nullptr : &rep);
            }
        }
    }
    creator.endCell();
    creator.endFile();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "usage: %s OUTPUT_DIRECTORY [--side N] (2 <= N <= 1000)\n", argv[0]);
        return 2;
    }
    unsigned side = 1000;
    if (argc == 4) {
        if (std::string(argv[2]) != "--side") return 2;
        char* end = nullptr;
        const unsigned long value = std::strtoul(argv[3], &end, 10);
        if (!argv[3][0] || *end || value < 2 || value > 1000) return 2;
        side = static_cast<unsigned>(value);
    }
    const std::string directory(argv[1]);
    try {
        const Oasis::PointList polygon = polygon1000();
        for (int type = 1; type <= 11; ++type) {
            char name[32];
            std::snprintf(name, sizeof(name), "/rep-%02d.oas", type);
            writeLayout(directory + name, type, side, polygon);
            std::printf("rep-%02d.oas: %u instances\n", type, side * side);
        }
        writeLayout(directory + "/no-repetition.oas", 0, side, polygon);
        std::printf("no-repetition.oas: %u explicit polygons\n", side * side);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "generation failed: %s\n", e.what());
        return 1;
    }
    return 0;
}
