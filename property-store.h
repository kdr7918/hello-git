#ifndef PROPERTY_EXAMPLE_STORE_H
#define PROPERTY_EXAMPLE_STORE_H

#include "property.h"
#include <algorithm>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace property_example {

struct PropertyListRef {
    uint64_t offset = 0;
    uint64_t byteSize = 0;
    uint64_t count = 0;
};

// Layout owns ONE store. Shape/Cell/TaggedPtr layouts are unchanged.
// Build: synchronized append. Query: freeze(), then externally synchronized
// handoff to readers. No editing, reset or unfreeze API in this example.
class PropertyStore {
public:
    using Owner = const void*; // ShapeBase* converts directly; never a payload pointer.

    uint64_t internName(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        requireBuilding();
        const auto it = nameIds_.find(name);
        if (it != nameIds_.end()) return it->second;
        const uint64_t id = names_.size();
        names_.push_back(name);
        try { nameIds_.emplace(name, id); }
        catch (...) { names_.pop_back(); throw; }
        return id;
    }

    void setElement(Owner owner, const PropertyList& properties) {
        if (!owner) throw std::invalid_argument("null element owner");
        std::lock_guard<std::mutex> lock(mutex_);
        requireBuilding();
        if (!properties.count) return;
        storeEntry(elements_, owner, properties);
    }
    void setCell(uint32_t cellId, const PropertyList& properties) {
        std::lock_guard<std::mutex> lock(mutex_);
        requireBuilding();
        if (!properties.count) return;
        storeEntry(cells_, cellId, properties);
    }
    void setFile(const PropertyList& properties) {
        std::lock_guard<std::mutex> lock(mutex_);
        requireBuilding();
        if (fileSet_) throw std::logic_error("file properties already stored");
        if (properties.count) file_ = appendBytes(properties);
        fileSet_ = true;
    }

    // Call ONCE after all import workers have joined, not per-cell/per-worker.
    // Each owner must be committed once; merge its properties in scratch first.
    void freeze() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frozen_) return;
        sortAndCheck(elements_);
        sortAndCheck(cells_);
        // Drop the build-only name hash index; names_ remains alive.
        std::unordered_map<std::string, uint64_t>().swap(nameIds_);
        frozen_ = true;
    }

    PropertyListRef element(Owner owner) const {
        requireFrozen();
        return lookup(elements_, owner);
    }
    PropertyListRef cell(uint32_t id) const {
        requireFrozen();
        return lookup(cells_, id);
    }
    PropertyListRef file() const { requireFrozen(); return file_; }
    const std::string& name(uint64_t id) const {
        requireFrozen();
        if (id >= names_.size()) throw std::out_of_range("property name ID");
        return names_[static_cast<size_t>(id)];
    }

    // Only pass a ref obtained from THIS store; refs are not cross-store IDs.
    template<class Visitor>
    void forEach(PropertyListRef ref, Visitor visit) const {
        requireFrozen();
        if (ref.offset > bytes_.size() || ref.byteSize > bytes_.size() - ref.offset)
            throw std::out_of_range("property list range");
        if (ref.count == 0) {
            if (ref.byteSize != 0) throw std::runtime_error("invalid empty list");
            return;
        }
        if (ref.byteSize == 0) throw std::runtime_error("empty property encoding");
        const uint8_t* p = bytes_.data() + static_cast<size_t>(ref.offset);
        const uint8_t* end = p + static_cast<size_t>(ref.byteSize);
        for (uint64_t i = 0; i < ref.count; ++i) {
            if (p == end) throw std::runtime_error("missing property key kind");
            const auto kind = static_cast<KeyKind>(*p++);
            if (kind != KeyKind::OasisName && kind != KeyKind::GdsAttribute)
                throw std::runtime_error("bad property key kind");
            const uint64_t key = get64(p, end);
            if (kind == KeyKind::OasisName && key >= names_.size())
                throw std::runtime_error("unknown property name");
            if (p == end || *p > 1) throw std::runtime_error("bad property flags");
            const bool standard = *p++ != 0;
            const uint64_t count = get64(p, end);
            const uint64_t size = get64(p, end);
            if (size > uint64_t(end - p)) throw std::runtime_error("bad property size");
            visit(PropertyView{kind, key, standard, count, p, static_cast<size_t>(size)});
            p += static_cast<size_t>(size);
        }
        if (p != end) throw std::runtime_error("extra property bytes");
    }

private:
    template<class Key> struct Entry {
        Key owner;
        uint64_t listId;
    };

    PropertyListRef appendBytes(const PropertyList& src) {
        PropertyListRef ref;
        ref.offset = bytes_.size();
        ref.byteSize = src.bytes.size();
        ref.count = src.count;
        if (src.bytes.size() > bytes_.max_size() - bytes_.size())
            throw std::length_error("property pool too large");
        bytes_.insert(bytes_.end(), src.bytes.begin(), src.bytes.end());
        return ref;
    }
    template<class Key>
    void storeEntry(std::vector<Entry<Key>>& index, Key owner, const PropertyList& src) {
        const size_t oldBytes = bytes_.size(), oldLists = lists_.size();
        try {
            const PropertyListRef ref = appendBytes(src);
            lists_.push_back(ref);
            index.push_back(Entry<Key>{owner, static_cast<uint64_t>(oldLists)});
        } catch (...) {
            bytes_.resize(oldBytes);
            lists_.resize(oldLists);
            throw;
        }
    }
    template<class Key> static void sortAndCheck(std::vector<Entry<Key>>& entries) {
        const std::less<Key> less;
        std::sort(entries.begin(), entries.end(), [&](const Entry<Key>& a, const Entry<Key>& b) {
            return less(a.owner, b.owner);
        });
        for (size_t i = 1; i < entries.size(); ++i)
            if (entries[i - 1].owner == entries[i].owner)
                throw std::logic_error("owner committed twice");
    }
    template<class Key>
    PropertyListRef lookup(const std::vector<Entry<Key>>& entries, Key owner) const {
        const std::less<Key> less;
        const auto it = std::lower_bound(entries.begin(), entries.end(), owner,
            [&](const Entry<Key>& entry, Key key) { return less(entry.owner, key); });
        return it == entries.end() || it->owner != owner ? PropertyListRef() :
            lists_.at(static_cast<size_t>(it->listId));
    }
    void requireBuilding() const {
        if (frozen_) throw std::logic_error("property store is frozen");
    }
    void requireFrozen() const {
        // Caller must not race freeze() / destruction with any reader.
        if (!frozen_) throw std::logic_error("freeze before property lookup");
    }

    std::mutex mutex_;
    bool frozen_ = false;
    bool fileSet_ = false;
    std::unordered_map<std::string, uint64_t> nameIds_;
    std::vector<std::string> names_;
    Bytes bytes_;
    std::vector<PropertyListRef> lists_;
    PropertyListRef file_;
    std::vector<Entry<Owner>> elements_; // sorted address -> listId, not unordered_map
    std::vector<Entry<uint32_t>> cells_; // sorted cellId -> listId
};

} // namespace property_example
#endif
