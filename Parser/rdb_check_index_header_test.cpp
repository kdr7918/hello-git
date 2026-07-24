#include "rdb_check_index.hpp"

int main() {
    const rdb::FastCheckIndexParser parser;
    rdb::FastCheckIndexOptions options;
    options.progress_callback = [](int) {};
    (void)parser;
    return 0;
}
