#include "rdb_types.hpp"

RDB_ALL_DATA::RDB_ALL_DATA()
    : index(0U),
      ordinal(0U),
      ew(0.0),
      el(0.0),
      pa(0.0),
      pp(0.0),
      type('p'),
      tmp_val(0.0),
      count_extra_item(0) {}

void RDB_ALL_DATA::AddExtraItem(const RDB_EXTRA_ITEM& item) {
    if (count_extra_item < MaxInlineExtraItems) {
        extra_list[count_extra_item] = item;
    } else {
        extra_overflow.push_back(item);
    }
    ++count_extra_item;
}

QVariant RDB_ALL_DATA::PropertyValue(const QString& key) const {
    const int inlineCount = count_extra_item < MaxInlineExtraItems
        ? count_extra_item : MaxInlineExtraItems;
    for (int i = 0; i < inlineCount; ++i) {
        if (extra_list[i].key == key) return extra_list[i].value;
    }
    for (std::size_t i = 0; i < extra_overflow.size(); ++i) {
        if (extra_overflow[i].key == key) return extra_overflow[i].value;
    }
    return QVariant();
}

QStringList RDB_ALL_DATA::HeaderList() const {
    QStringList headers;
    const int inlineCount = count_extra_item < MaxInlineExtraItems
        ? count_extra_item : MaxInlineExtraItems;
    for (int i = 0; i < inlineCount; ++i) {
        if (!extra_list[i].key.isEmpty() &&
            !headers.contains(extra_list[i].key)) {
            headers.append(extra_list[i].key);
        }
    }
    for (std::size_t i = 0; i < extra_overflow.size(); ++i) {
        if (!extra_overflow[i].key.isEmpty() &&
            !headers.contains(extra_overflow[i].key)) {
            headers.append(extra_overflow[i].key);
        }
    }
    return headers;
}
