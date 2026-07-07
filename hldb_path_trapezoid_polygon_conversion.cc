// Standalone reference extraction of HLDB viewer's Path/Trapezoid-to-polygon
// rendering conversions.
//
// Source inspected in local HLDB workspace:
//   /home/kdr0324/workspace/hldb/cpp/src/viewer_session.cpp
//
// This file is intentionally self-contained: it does not depend on HLDB headers.
// It mirrors the geometry policy used before SVG emission:
//   - Path centerline + half-width + start/end extensions -> filled outline polygon
//   - Trapezoid box + deltas + orientation flag -> four polygon corners

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

struct Point {
  std::int64_t x = 0;
  std::int64_t y = 0;

  friend bool operator==(const Point&, const Point&) = default;
};

struct PathShape {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::uint64_t half_width = 0;
  std::int64_t start_extension = 0;
  std::int64_t end_extension = 0;
  // HLDB stores an encoded point_list pointer here. In this standalone file the
  // caller passes already-decoded absolute centerline points instead.
};

struct TrapezoidShape {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::int64_t delta_a = 0;
  std::int64_t delta_b = 0;
  std::uint8_t vertical = 0;
};

struct FloatPoint {
  long double x = 0.0L;
  long double y = 0.0L;
};

FloatPoint operator+(FloatPoint lhs, FloatPoint rhs) noexcept {
  return FloatPoint{lhs.x + rhs.x, lhs.y + rhs.y};
}

FloatPoint operator-(FloatPoint lhs, FloatPoint rhs) noexcept {
  return FloatPoint{lhs.x - rhs.x, lhs.y - rhs.y};
}

FloatPoint operator*(FloatPoint point, long double scale) noexcept {
  return FloatPoint{point.x * scale, point.y * scale};
}

long double length(FloatPoint point) noexcept {
  return std::sqrt(point.x * point.x + point.y * point.y);
}

std::optional<FloatPoint> normalized(FloatPoint point) noexcept {
  const long double len = length(point);
  if (len <= 1e-12L) return std::nullopt;
  return FloatPoint{point.x / len, point.y / len};
}

long double cross(FloatPoint lhs, FloatPoint rhs) noexcept {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

std::optional<FloatPoint> line_intersection(FloatPoint origin_a,
                                            FloatPoint direction_a,
                                            FloatPoint origin_b,
                                            FloatPoint direction_b) noexcept {
  const long double denominator = cross(direction_a, direction_b);
  if (std::abs(denominator) <= 1e-12L) return std::nullopt;
  const long double t = cross(origin_b - origin_a, direction_b) / denominator;
  return origin_a + direction_a * t;
}

std::int64_t clamp_to_i64(long double value) noexcept {
  const auto min_value =
      static_cast<long double>(std::numeric_limits<std::int64_t>::min());
  const auto max_value =
      static_cast<long double>(std::numeric_limits<std::int64_t>::max());
  if (value <= min_value) return std::numeric_limits<std::int64_t>::min();
  if (value >= max_value) return std::numeric_limits<std::int64_t>::max();
  return static_cast<std::int64_t>(value);
}

std::int64_t round_to_i64(long double value) {
  if (!std::isfinite(value)) return 0;
  return clamp_to_i64(std::round(value));
}

void append_unique_point(std::vector<Point>& points, Point point) {
  if (!points.empty() && points.back() == point) return;
  points.push_back(point);
}

// Convert an OASIS/HLDB Path into the filled polygon that the viewer renders.
//
// center_points are decoded absolute DBU points. In HLDB they come from:
//   layout.decode_point_list(path.point_list, path.x, path.y, center_points)
std::vector<Point> path_outline_points(const PathShape& path,
                                       const std::vector<Point>& center_points) {
  if (center_points.size() < 2 || path.half_width == 0) return {};

  std::vector<FloatPoint> centers;
  centers.reserve(center_points.size());
  for (const auto& point : center_points) {
    FloatPoint current{static_cast<long double>(point.x),
                       static_cast<long double>(point.y)};
    if (centers.empty() || length(current - centers.back()) > 1e-12L) {
      centers.push_back(current);
    }
  }
  if (centers.size() < 2) return {};

  const auto first_direction = normalized(centers[1] - centers[0]);
  const auto last_direction =
      normalized(centers[centers.size() - 1U] - centers[centers.size() - 2U]);
  if (!first_direction || !last_direction) return {};

  centers.front() = centers.front() -
                    *first_direction *
                        static_cast<long double>(path.start_extension);
  centers.back() = centers.back() +
                   *last_direction *
                       static_cast<long double>(path.end_extension);

  std::vector<FloatPoint> directions;
  std::vector<FloatPoint> normals;
  directions.reserve(centers.size() - 1U);
  normals.reserve(centers.size() - 1U);
  for (std::size_t i = 1; i < centers.size(); ++i) {
    const auto direction = normalized(centers[i] - centers[i - 1U]);
    if (!direction) return {};
    directions.push_back(*direction);
    normals.push_back(FloatPoint{-direction->y, direction->x});
  }

  const long double half_width = static_cast<long double>(std::min<std::uint64_t>(
      path.half_width,
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
  const long double miter_limit = std::max(half_width * 8.0L, half_width + 1.0L);

  const auto offset_vertex = [&](std::size_t index,
                                 long double side) -> FloatPoint {
    if (index == 0) return centers[index] + normals.front() * half_width * side;
    if (index + 1U == centers.size()) {
      return centers[index] + normals.back() * half_width * side;
    }

    const FloatPoint previous_origin =
        centers[index] + normals[index - 1U] * half_width * side;
    const FloatPoint next_origin =
        centers[index] + normals[index] * half_width * side;
    const auto intersection =
        line_intersection(previous_origin, directions[index - 1U], next_origin,
                          directions[index]);
    if (intersection &&
        length(*intersection - centers[index]) <= miter_limit) {
      return *intersection;
    }

    FloatPoint blended = normals[index - 1U] + normals[index];
    const auto blended_normal = normalized(blended);
    if (blended_normal) {
      return centers[index] + *blended_normal * half_width * side;
    }
    return centers[index] + normals[index] * half_width * side;
  };

  std::vector<Point> outline;
  outline.reserve(centers.size() * 2U);
  for (std::size_t i = 0; i < centers.size(); ++i) {
    const auto point = offset_vertex(i, 1.0L);
    append_unique_point(outline,
                        Point{round_to_i64(point.x), round_to_i64(point.y)});
  }
  for (std::size_t i = centers.size(); i > 0; --i) {
    const auto point = offset_vertex(i - 1U, -1.0L);
    append_unique_point(outline,
                        Point{round_to_i64(point.x), round_to_i64(point.y)});
  }
  if (outline.size() >= 2 && outline.front() == outline.back()) {
    outline.pop_back();
  }
  return outline.size() >= 3 ? outline : std::vector<Point>{};
}

// Convert an HLDB Trapezoid shape into the four polygon corners rendered by SVG.
std::vector<Point> trapezoid_points(const TrapezoidShape& trapezoid) {
  const auto width = static_cast<std::int64_t>(
      std::min<std::uint64_t>(
          trapezoid.width,
          static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())));
  const auto height = static_cast<std::int64_t>(
      std::min<std::uint64_t>(
          trapezoid.height,
          static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())));
  const auto x0 = trapezoid.x;
  const auto y0 = trapezoid.y;
  const auto x1 = x0 + width;
  const auto y1 = y0 + height;

  if (trapezoid.vertical != 0) {
    return std::vector<Point>{{x0, y0 + trapezoid.delta_a},
                              {x0, y1 + trapezoid.delta_b},
                              {x1, y1},
                              {x1, y0}};
  }
  return std::vector<Point>{{x0, y0},
                            {x1, y0},
                            {x1 + trapezoid.delta_b, y1},
                            {x0 + trapezoid.delta_a, y1}};
}

void print_points(const char* label, const std::vector<Point>& points) {
  std::cout << label << ":";
  for (const auto& point : points) {
    std::cout << " (" << point.x << "," << point.y << ")";
  }
  std::cout << '\n';
}

int main() {
  const PathShape path{.half_width = 10, .start_extension = 5, .end_extension = 5};
  const std::vector<Point> centerline{{0, 0}, {100, 0}, {100, 80}};
  print_points("path_outline", path_outline_points(path, centerline));

  const TrapezoidShape horizontal{.x = 0,
                                  .y = 0,
                                  .width = 100,
                                  .height = 40,
                                  .delta_a = 10,
                                  .delta_b = -15,
                                  .vertical = 0};
  print_points("trapezoid_horizontal", trapezoid_points(horizontal));

  const TrapezoidShape vertical{.x = 0,
                                .y = 0,
                                .width = 100,
                                .height = 40,
                                .delta_a = 10,
                                .delta_b = -15,
                                .vertical = 1};
  print_points("trapezoid_vertical", trapezoid_points(vertical));
}
