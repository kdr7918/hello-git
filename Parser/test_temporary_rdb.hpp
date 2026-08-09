#ifndef RDB_TEST_TEMPORARY_RDB_HPP
#define RDB_TEST_TEMPORARY_RDB_HPP

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace rdb_test {

inline std::string temporary_directory() {
    const char* const variables[] = {"TMPDIR", "TEMP", "TMP"};
    for (std::size_t i = 0; i < sizeof(variables) / sizeof(variables[0]); ++i) {
        const char* const value = std::getenv(variables[i]);
        if (value != 0 && *value != '\0') return value;
    }
    return ".";
}

inline std::string make_temporary_path(const char* prefix) {
    static std::atomic<unsigned long> sequence(0UL);
    const unsigned long serial = sequence.fetch_add(1UL);
    const long long timestamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();

    std::ostringstream path;
    path << temporary_directory();
    const std::string directory = path.str();
    if (!directory.empty() && directory[directory.size() - 1U] != '/' &&
        directory[directory.size() - 1U] != '\\') {
        path << '/';
    }
    path << prefix << '-' << timestamp << '-' << serial << ".rdb";
    return path.str();
}

class TemporaryRdb {
public:
    TemporaryRdb(const char* prefix, const std::string& contents)
        : path_(make_temporary_path(prefix)) {
        overwrite(contents);
    }

    ~TemporaryRdb() { remove_file(); }

    const std::string& path() const { return path_; }

    void overwrite(const std::string& contents) const {
        std::ofstream output(path_.c_str(), std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create temporary RDB file '" + path_ + "'");
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) {
            throw std::runtime_error("cannot write temporary RDB file '" + path_ + "'");
        }
    }

    void append(const std::string& contents) const {
        std::ofstream output(path_.c_str(), std::ios::binary | std::ios::app);
        if (!output) {
            throw std::runtime_error("cannot open temporary RDB file '" + path_ + "'");
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) {
            throw std::runtime_error("cannot append temporary RDB file '" + path_ + "'");
        }
    }

    bool remove_file() {
        if (path_.empty()) return true;
        if (std::remove(path_.c_str()) != 0) return false;
        path_.clear();
        return true;
    }

    bool replace(TemporaryRdb& destination) {
        const std::string destination_path = destination.path_;
        if (path_.empty() || !destination.remove_file()) return false;
        if (std::rename(path_.c_str(), destination_path.c_str()) != 0) return false;
        destination.path_ = destination_path;
        path_.clear();
        return true;
    }

private:
    TemporaryRdb(const TemporaryRdb&);
    TemporaryRdb& operator=(const TemporaryRdb&);

    std::string path_;
};

} // namespace rdb_test

#endif
