#include <functional.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("MSL member-function adapter contract failed");
  }
}

struct Point {
  int x;
  explicit Point(int value) : x(value) {}
  Point(const Point&) = delete;
};

struct Area {
  int limit;
  int updates = 0;

  void update() { ++updates; }
  int count() const { return updates; }
  bool contains(const Point& point) const { return point.x < limit; }
  int& value() { return limit; }
};

bool unused(const Area* area) { return area->updates == 0; }
int& pointValue(Point& point) { return point.x; }
int pointDifference(const Point& first, const Point& second) { return first.x - second.x; }
} // namespace

int main() {
  auto first = Area{4};
  auto last = Area{8};
  const auto areas = std::array{&first, &last};
  std::for_each(areas.begin(), areas.end(), std::mem_func(&Area::update));
  require(std::mem_func(&Area::count)(&first) == 1);
  require(std::mem_func(&Area::count)(static_cast<const Area*>(&last)) == 1);

  // The exact AreaObj find_in call binds a const vector reference. Keep the
  // original identity even when the source object is noncopyable or changes.
  auto point = Point{6};
  const auto contains = std::bind2nd(std::mem_func(&Area::contains), point);
  require(!contains(&first) && contains(&last));
  point.x = 2;
  require(contains(&first));
  require(std::rfind_if(areas.end() - 1, areas.begin(), contains) == areas.end() - 1);

  static_assert(std::is_same_v<decltype(std::mem_func(&Area::value)(&first)), int&>);
  std::mem_func(&Area::value)(&first) = 9;
  require(first.limit == 9);

  // Preserve the original pointer predicate's base/const conversion inside an
  // algorithm, plus noncopyable reference arguments and reference results.
  last.updates = 0;
  const auto predicate = std::ptr_fun(&unused);
  require(std::find_if(areas.begin(), areas.end(), predicate) == areas.begin() + 1);
  const auto value = std::ptr_fun(&pointValue);
  static_assert(std::is_same_v<decltype(value(point)), int&>);
  value(point) = 5;
  Point another{2};
  require(std::ptr_fun(&pointDifference)(point, another) == 3);
  std::cout << "[ok] MSL member adapters, reference-preserving bind2nd and ptr_fun\n";
}
