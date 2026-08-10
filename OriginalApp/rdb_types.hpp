#ifndef ORIGINAL_APP_RDB_TYPES_HPP
#define ORIGINAL_APP_RDB_TYPES_HPP

#include <QMetaType>
#include <QRectF>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

struct RDB_EXTRA_ITEM {
    QString key;
    QVariant value;
};

struct RDB_DATA {
    quint64 index;
    QString name;
    QString comment;
    quint64 count;
    quint64 seek_point;
    quint64 coord_offset;

    RDB_DATA()
        : index(0U), count(0U), seek_point(0U), coord_offset(0U) {}
};

typedef QSharedPointer<RDB_DATA> RDB_DATA_PTR;
typedef std::vector<RDB_DATA_PTR> RDB_DATA_LIST;

struct RDB_ALL_DATA {
    enum { MaxInlineExtraItems = 20 };

    quint64 index;
    quint32 ordinal;
    double ew;
    double el;
    double pa;
    double pp;
    char type;
    double tmp_val;
    int count_extra_item;
    RDB_EXTRA_ITEM extra_list[MaxInlineExtraItems];
    // The original interface had only extra_list[20]. Overflow storage keeps
    // valid RDB properties instead of silently discarding the 21st item.
    std::vector<RDB_EXTRA_ITEM> extra_overflow;
    std::vector<qint64> vertex_list;
    QRectF bbox_;
    RDB_DATA_PTR check;

    RDB_ALL_DATA();

    void AddExtraItem(const RDB_EXTRA_ITEM& item);
    QVariant PropertyValue(const QString& key) const;
    QStringList HeaderList() const;
};

typedef QSharedPointer<RDB_ALL_DATA> RDB_ALL_DATA_PTR;
typedef std::vector<RDB_ALL_DATA_PTR> RDB_ALL_DATA_LIST;

struct RDB_INDEX_RESULT {
    double dbu;
    QString topcell;
    RDB_DATA_LIST chips;

    RDB_INDEX_RESULT() : dbu(0.0) {}
};

typedef QSharedPointer<RDB_INDEX_RESULT> RDB_INDEX_RESULT_PTR;

Q_DECLARE_METATYPE(RDB_INDEX_RESULT_PTR)
Q_DECLARE_METATYPE(RDB_ALL_DATA_LIST)

#endif // ORIGINAL_APP_RDB_TYPES_HPP
