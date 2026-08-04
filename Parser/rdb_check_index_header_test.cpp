#include "rdb_check_index.hpp"

#include <type_traits>
#include <utility>

static_assert(std::is_same<
                  decltype(std::declval<const rdb::FastCheckIndexParser&>().parse_database(-1)),
                  rdb::CheckIndexDatabase>::value,
              "fast index parser must retain its descriptor overload");

int main() {
    const rdb::FastCheckIndexParser parser;
    rdb::FastCheckIndexOptions options;
    options.progress_callback = [](int) {};
    (void)parser;
    return 0;
}
