#include "rdb_parser_workers.hpp"

#include "rdb_check_detail.hpp"
#include "rdb_check_index.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>

BgParser::BgParser(
    CalibreTextDock* doc,
    const QString& path,
    const std::shared_ptr<std::atomic<bool> >& interrupt,
    QObject* parent)
    : QObject(parent),
      doc_(doc),
      path_(path),
      size_(static_cast<quint64>(QFileInfo(path).size())),
      interrupt_(interrupt) {}

void BgParser::run() {
    Q_UNUSED(doc_)
    Q_UNUSED(size_)
    try {
        const QByteArray encodedPath = QFile::encodeName(path_);
        rdb::FastCheckIndexOptions options;
        options.progress_callback = [this](int progress) {
            emit ProgressChanged(progress);
        };
        options.is_cancelled = [this]() {
            return interrupt_ && interrupt_->load();
        };

        const rdb::CheckIndexDatabase parsed =
            rdb::FastCheckIndexParser().parse_database(
                encodedPath.constData(), options);
        if (interrupt_ && interrupt_->load()) {
            emit ParsingCancelled();
            return;
        }

        RDB_INDEX_RESULT_PTR result(new RDB_INDEX_RESULT);
        result->dbu = parsed.database_precision;
        result->topcell = QString::fromUtf8(
            parsed.top_cell_name.data(),
            static_cast<int>(parsed.top_cell_name.size()));
        result->chips.reserve(parsed.checks.size());
        quint64 coordOffset = 0U;
        for (std::size_t i = 0; i < parsed.checks.size(); ++i) {
            const rdb::CheckIndexEntry& source = parsed.checks[i];
            RDB_DATA_PTR check(new RDB_DATA);
            check->index = static_cast<quint64>(i);
            check->name = QString::fromUtf8(
                source.name.data(), static_cast<int>(source.name.size()));
            check->comment = QString::fromUtf8(
                source.comment.data(), static_cast<int>(source.comment.size()));
            check->count = static_cast<quint64>(source.geometry_count);
            check->seek_point = static_cast<quint64>(source.offset);
            check->coord_offset = coordOffset;
            if (check->count >
                std::numeric_limits<quint64>::max() - coordOffset) {
                throw std::length_error("RDB coordinate offset exceeds 64-bit capacity");
            }
            coordOffset += check->count;
            result->chips.push_back(check);
        }
        emit CompleteBgParsing(result);
    } catch (const rdb::ScanCancelled&) {
        emit ParsingCancelled();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Check index parser error"));
    }
}

RDBDetailParser::RDBDetailParser(
    const QString& path,
    const RDB_DATA_LIST& chips,
    const std::shared_ptr<std::atomic<bool> >& interrupt,
    QObject* parent)
    : QObject(parent),
      path_(path),
      chips_(chips),
      next_result_index_(0U),
      interrupt_(interrupt) {}

void RDBDetailParser::run() {
    try {
        const QByteArray encodedPath = QFile::encodeName(path_);
        rdb::CheckDetailFile detailFile(encodedPath.constData());
        for (std::size_t checkNumber = 0;
             checkNumber < chips_.size();
             ++checkNumber) {
            if (interrupt_ && interrupt_->load()) {
                emit ParsingCancelled();
                return;
            }
            const RDB_DATA_PTR check = chips_[checkNumber];
            if (!check) continue;

            rdb::CheckDetailBatchOptions options;
            options.batch_size = 10000U;
            options.is_cancelled = [this]() {
                return interrupt_ && interrupt_->load();
            };
            options.batch_callback =
                [this, check](const std::vector<rdb::DetailResult>& batch) {
                    if (interrupt_ && interrupt_->load()) return;
                    RDB_ALL_DATA_LIST converted;
                    converted.reserve(batch.size());
                    QStringList headers;
                    for (std::size_t i = 0; i < batch.size(); ++i) {
                        RDB_ALL_DATA_PTR value = ConvertResult(batch[i], check);
                        const QStringList valueHeaders = value->HeaderList();
                        for (int header = 0; header < valueHeaders.size(); ++header) {
                            if (!headers.contains(valueHeaders[header])) {
                                headers.append(valueHeaders[header]);
                            }
                        }
                        converted.push_back(value);
                    }
                    emit BatchReady(check->index, converted, headers);
                };

            const rdb::CheckDetailBatchResult result =
                detailFile.parse_at_batches(
                    static_cast<rdb::CheckOffset>(check->seek_point),
                    options);
            if (!result.completed || (interrupt_ && interrupt_->load())) {
                emit ParsingCancelled();
                return;
            }
            emit CheckParsingComplete(check->index);
        }
        emit Complete();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown RDB detail parser error"));
    }
}

RDB_ALL_DATA_PTR RDBDetailParser::ConvertResult(
    const rdb::DetailResult& source,
    const RDB_DATA_PTR& check) {
    RDB_ALL_DATA_PTR value(new RDB_ALL_DATA);
    value->index = next_result_index_++;
    value->ordinal = source.ordinal;
    value->type = source.kind == rdb::ResultKind::Polygon ? 'p' : 'e';
    value->check = check;

    for (std::size_t i = 0; i < source.properties.size(); ++i) {
        const rdb::DetailTag& property = source.properties[i];
        RDB_EXTRA_ITEM item;
        item.key = QString::fromUtf8(
            property.id.data(), static_cast<int>(property.id.size()));
        const QString payload = QString::fromUtf8(
            property.payload.data(),
            static_cast<int>(property.payload.size()));
        bool numeric = false;
        const double number = payload.toDouble(&numeric);
        item.value = numeric ? QVariant(number) : QVariant(payload);
        value->AddExtraItem(item);

        if (numeric && item.key.compare(
                QStringLiteral("EW"), Qt::CaseInsensitive) == 0) {
            value->ew = number;
        } else if (numeric && item.key.compare(
                       QStringLiteral("EL"), Qt::CaseInsensitive) == 0) {
            value->el = number;
        } else if (numeric && item.key.compare(
                       QStringLiteral("PA"), Qt::CaseInsensitive) == 0) {
            value->pa = number;
        } else if (numeric && item.key.compare(
                       QStringLiteral("PP"), Qt::CaseInsensitive) == 0) {
            value->pp = number;
        }
    }

    if (source.kind == rdb::ResultKind::Polygon) {
        value->vertex_list.reserve(source.vertices.size() * 2U);
        for (std::size_t i = 0; i < source.vertices.size(); ++i) {
            value->vertex_list.push_back(
                static_cast<qint64>(source.vertices[i].x));
            value->vertex_list.push_back(
                static_cast<qint64>(source.vertices[i].y));
        }
    } else {
        value->vertex_list.reserve(source.edges.size() * 4U);
        for (std::size_t i = 0; i < source.edges.size(); ++i) {
            value->vertex_list.push_back(
                static_cast<qint64>(source.edges[i].first.x));
            value->vertex_list.push_back(
                static_cast<qint64>(source.edges[i].first.y));
            value->vertex_list.push_back(
                static_cast<qint64>(source.edges[i].second.x));
            value->vertex_list.push_back(
                static_cast<qint64>(source.edges[i].second.y));
        }
    }
    UpdateBoundingBox(*value);
    return value;
}

void RDBDetailParser::UpdateBoundingBox(RDB_ALL_DATA& value) const {
    if (value.vertex_list.size() < 2U) {
        value.bbox_ = QRectF();
        return;
    }
    qint64 minimumX = value.vertex_list[0];
    qint64 maximumX = value.vertex_list[0];
    qint64 minimumY = value.vertex_list[1];
    qint64 maximumY = value.vertex_list[1];
    for (std::size_t i = 2U; i + 1U < value.vertex_list.size(); i += 2U) {
        minimumX = std::min(minimumX, value.vertex_list[i]);
        maximumX = std::max(maximumX, value.vertex_list[i]);
        minimumY = std::min(minimumY, value.vertex_list[i + 1U]);
        maximumY = std::max(maximumY, value.vertex_list[i + 1U]);
    }
    const qreal left = static_cast<qreal>(minimumX);
    const qreal top = static_cast<qreal>(minimumY);
    const qreal right = static_cast<qreal>(maximumX);
    const qreal bottom = static_cast<qreal>(maximumY);
    value.bbox_ = QRectF(left, top, right - left, bottom - top);
}
