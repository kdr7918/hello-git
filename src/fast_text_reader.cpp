#include "fast_text_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

bool isSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

} // namespace

ByteView ByteView::left(int count) const
{
    return ByteView(m_data, qBound(0, count, m_size));
}

ByteView ByteView::mid(int position, int count) const
{
    const int start = qBound(0, position, m_size);
    const int available = m_size - start;
    const int length = count < 0 ? available : qBound(0, count, available);
    return ByteView(m_data + start, length);
}

ByteView ByteView::trimmed() const
{
    int first = 0;
    int last = m_size;
    while (first < last && isSpace(m_data[first]))
        ++first;
    while (last > first && isSpace(m_data[last - 1]))
        --last;
    return ByteView(m_data + first, last - first);
}

bool ByteView::startsWith(char value) const
{
    return m_size > 0 && m_data[0] == value;
}

bool ByteView::startsWith(ByteView value) const
{
    return value.m_size <= m_size &&
           (value.m_size == 0 || std::memcmp(m_data, value.m_data, value.m_size) == 0);
}

int ByteView::find(char value, int from) const
{
    const int start = qBound(0, from, m_size);
    const void *found = std::memchr(m_data + start,
                                    static_cast<unsigned char>(value),
                                    static_cast<size_t>(m_size - start));
    return found ? static_cast<int>(static_cast<const char *>(found) - m_data) : -1;
}

int ByteView::find(ByteView value, int from) const
{
    const int start = qBound(0, from, m_size);
    if (value.isEmpty())
        return start;
    if (value.size() > m_size - start)
        return -1;

    const char *found = std::search(m_data + start,
                                    m_data + m_size,
                                    value.data(),
                                    value.data() + value.size());
    return found == m_data + m_size ? -1 : static_cast<int>(found - m_data);
}

void ByteView::split(char delimiter,
                     QVector<ByteView> *output,
                     bool keepEmpty) const
{
    output->clear();
    int expected = 1;
    for (int position = find(delimiter); position >= 0;
         position = find(delimiter, position + 1)) {
        ++expected;
    }
    output->reserve(expected);
    forEachSplit(delimiter, [output](ByteView field) { output->append(field); },
                 keepEmpty);
}

bool ByteView::toInt64(qint64 *value) const
{
    const ByteView input = trimmed();
    if (input.isEmpty())
        return false;

    int position = 0;
    bool negative = false;
    if (input.at(position) == '-' || input.at(position) == '+') {
        negative = input.at(position) == '-';
        if (++position == input.size())
            return false;
    }

    const quint64 positiveLimit = static_cast<quint64>(
        std::numeric_limits<qint64>::max());
    const quint64 limit = negative ? positiveLimit + 1u : positiveLimit;
    quint64 parsed = 0;
    for (; position < input.size(); ++position) {
        const unsigned int digit = static_cast<unsigned int>(input.at(position) - '0');
        if (digit > 9u || parsed > (limit - digit) / 10u)
            return false;
        parsed = parsed * 10u + digit;
    }

    if (negative) {
        *value = parsed == positiveLimit + 1u
            ? std::numeric_limits<qint64>::min()
            : -static_cast<qint64>(parsed);
    } else {
        *value = static_cast<qint64>(parsed);
    }
    return true;
}

bool ByteView::toUInt64(quint64 *value) const
{
    const ByteView input = trimmed();
    if (input.isEmpty())
        return false;

    int position = input.at(0) == '+' ? 1 : 0;
    if (position == input.size())
        return false;

    quint64 parsed = 0;
    const quint64 limit = std::numeric_limits<quint64>::max();
    for (; position < input.size(); ++position) {
        const unsigned int digit = static_cast<unsigned int>(input.at(position) - '0');
        if (digit > 9u || parsed > (limit - digit) / 10u)
            return false;
        parsed = parsed * 10u + digit;
    }
    *value = parsed;
    return true;
}

bool ByteView::toDouble(double *value) const
{
    const ByteView input = trimmed();
    if (input.isEmpty())
        return false;

    int position = 0;
    bool negative = false;
    if (input.at(position) == '-' || input.at(position) == '+') {
        negative = input.at(position) == '-';
        if (++position == input.size())
            return false;
    }

    double parsed = 0.0;
    bool hasDigit = false;
    while (position < input.size()) {
        const unsigned int digit = static_cast<unsigned int>(input.at(position) - '0');
        if (digit > 9u)
            break;
        parsed = parsed * 10.0 + digit;
        hasDigit = true;
        ++position;
    }

    if (position < input.size() && input.at(position) == '.') {
        ++position;
        double scale = 0.1;
        while (position < input.size()) {
            const unsigned int digit = static_cast<unsigned int>(input.at(position) - '0');
            if (digit > 9u)
                break;
            parsed += digit * scale;
            scale *= 0.1;
            hasDigit = true;
            ++position;
        }
    }
    if (!hasDigit)
        return false;

    int exponent = 0;
    bool exponentNegative = false;
    if (position < input.size() &&
        (input.at(position) == 'e' || input.at(position) == 'E')) {
        ++position;
        if (position < input.size() &&
            (input.at(position) == '-' || input.at(position) == '+')) {
            exponentNegative = input.at(position) == '-';
            ++position;
        }
        const int exponentStart = position;
        while (position < input.size()) {
            const unsigned int digit = static_cast<unsigned int>(input.at(position) - '0');
            if (digit > 9u)
                break;
            if (exponent < 10000)
                exponent = exponent * 10 + static_cast<int>(digit);
            ++position;
        }
        if (position == exponentStart)
            return false;
    }
    if (position != input.size())
        return false;

    if (exponent != 0)
        parsed *= std::pow(10.0, exponentNegative ? -exponent : exponent);
    if (negative)
        parsed = -parsed;
    if (!std::isfinite(parsed))
        return false;
    *value = parsed;
    return true;
}

QByteArray ByteView::toByteArray() const
{
    return QByteArray(m_data, m_size);
}

QString ByteView::toUtf8() const
{
    return QString::fromUtf8(m_data, m_size);
}

FieldCursor::FieldCursor(ByteView bytes, qint64 sourceOffset, char delimiter)
    : m_bytes(bytes),
      m_sourceOffset(sourceOffset),
      m_delimiter(delimiter),
      m_position(0),
      m_finished(false)
{
}

bool FieldCursor::next(ByteView *field)
{
    if (m_finished)
        return false;

    const int separator = m_bytes.find(m_delimiter, m_position);
    const int end = separator < 0 ? m_bytes.size() : separator;
    *field = m_bytes.mid(m_position, end - m_position);
    updateLastSpan(*field);
    if (separator < 0) {
        m_position = m_bytes.size();
        m_finished = true;
    } else {
        m_position = separator + 1;
    }
    return true;
}

bool FieldCursor::nextTrimmed(ByteView *field)
{
    ByteView untrimmed;
    if (!next(&untrimmed))
        return false;
    *field = untrimmed.trimmed();
    updateLastSpan(*field);
    return true;
}

bool FieldCursor::readInt64(qint64 *value)
{
    ByteView field;
    return nextTrimmed(&field) && field.toInt64(value);
}

bool FieldCursor::readUInt64(quint64 *value)
{
    ByteView field;
    return nextTrimmed(&field) && field.toUInt64(value);
}

bool FieldCursor::readDouble(double *value)
{
    ByteView field;
    return nextTrimmed(&field) && field.toDouble(value);
}

bool FieldCursor::readString(ByteView *value)
{
    return nextTrimmed(value);
}

bool FieldCursor::readUtf8(QString *value)
{
    ByteView field;
    if (!nextTrimmed(&field))
        return false;
    *value = field.toUtf8();
    return true;
}

ByteView FieldCursor::remaining() const
{
    return m_finished ? ByteView() : m_bytes.mid(m_position);
}

void FieldCursor::updateLastSpan(ByteView field)
{
    const qint64 relative = field.data() - m_bytes.data();
    m_lastSpan = SourceSpan(m_sourceOffset + relative,
                            m_sourceOffset + relative + field.size());
}

qint64 LineView::findAbsolute(char value, int from) const
{
    const int index = bytes.find(value, from);
    return index < 0 ? -1 : beginOffset + index;
}

bool LineView::findRegex(const QRegularExpression &expression, RegexHit *hit) const
{
    *hit = RegexHit();
    const QString subject = bytes.toUtf8();
    const QRegularExpressionMatch match = expression.match(subject);
    if (!match.hasMatch())
        return false;

    const QString prefix = subject.left(match.capturedStart());
    const QString captured = match.captured(0);
    hit->byteIndex = prefix.toUtf8().size();
    hit->byteLength = captured.toUtf8().size();
    hit->sourceOffset = beginOffset + hit->byteIndex;
    hit->captured = captured;
    hit->captures = match.capturedTexts();
    return true;
}

FastTextReader::FastTextReader(int bufferSize)
    : m_initialBufferSize(qMax(4096, bufferSize)),
      m_dataEnd(0),
      m_reclaimBegin(0),
      m_bufferOffset(0),
      m_fileSize(0),
      m_rangeBegin(0),
      m_rangeEnd(0),
      m_nextLineNumber(1),
      m_eof(false),
      m_started(false),
      m_cachedNextValid(false)
{
    m_buffer.resize(m_initialBufferSize);
}

FastTextReader::~FastTextReader()
{
    close();
}

bool FastTextReader::open(const QString &filePath)
{
    close();
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        m_errorString = m_file.errorString();
        return false;
    }
    m_fileSize = m_file.size();
    resetState(0, m_fileSize, 1);
    return true;
}

void FastTextReader::close()
{
    if (m_file.isOpen())
        m_file.close();
    m_dataEnd = 0;
    m_reclaimBegin = 0;
    m_bufferOffset = 0;
    m_fileSize = 0;
    m_rangeBegin = 0;
    m_rangeEnd = 0;
    m_eof = false;
    m_started = false;
    m_cachedNextValid = false;
    m_errorString.clear();
}

bool FastTextReader::setRange(qint64 beginOffset,
                              qint64 endOffset,
                              qint64 initialLineNumber)
{
    if (!m_file.isOpen()) {
        m_errorString = QStringLiteral("File is not open");
        return false;
    }
    const qint64 boundedEnd = endOffset < 0 ? m_fileSize : endOffset;
    if (beginOffset < 0 || beginOffset > m_fileSize ||
        boundedEnd < beginOffset || boundedEnd > m_fileSize) {
        m_errorString = QStringLiteral("Invalid byte range [%1, %2)")
                            .arg(beginOffset)
                            .arg(boundedEnd);
        return false;
    }
    if (!m_file.seek(beginOffset)) {
        m_errorString = m_file.errorString();
        return false;
    }
    resetState(beginOffset, boundedEnd, initialLineNumber);
    return true;
}

bool FastTextReader::seek(const SeekPoint &point, qint64 endOffset)
{
    return setRange(point.offset, endOffset, point.lineNumber);
}

SeekPoint FastTextReader::nextSeekPoint() const
{
    if (m_cachedNextValid) {
        return SeekPoint(m_bufferOffset + m_cachedNext.start,
                         m_cachedNext.lineNumber);
    }
    if (!m_started)
        return SeekPoint(m_rangeBegin, m_nextLineNumber);
    return SeekPoint(m_rangeEnd, m_nextLineNumber);
}

bool FastTextReader::fillMore(int *lineStart,
                              int *scanPosition,
                              LineBounds *protectedLine)
{
    if (m_dataEnd == m_buffer.size()) {
        if (m_reclaimBegin > 0) {
            const int removed = m_reclaimBegin;
            const int remaining = m_dataEnd - removed;
            std::memmove(m_buffer.data(),
                         m_buffer.constData() + removed,
                         static_cast<size_t>(remaining));
            m_bufferOffset += removed;
            m_dataEnd = remaining;
            *lineStart -= removed;
            *scanPosition -= removed;
            if (protectedLine) {
                protectedLine->start -= removed;
                protectedLine->contentEnd -= removed;
                protectedLine->next -= removed;
            }
            m_reclaimBegin = 0;
        } else {
            if (m_buffer.size() > std::numeric_limits<int>::max() / 2) {
                m_errorString = QStringLiteral("A line exceeds the addressable buffer size");
                return false;
            }
            m_buffer.resize(m_buffer.size() * 2);
        }
    }

    const qint64 bufferedEnd = m_bufferOffset + m_dataEnd;
    const qint64 rangeRemaining = m_rangeEnd - bufferedEnd;
    if (rangeRemaining <= 0) {
        m_eof = true;
        return false;
    }

    const qint64 available = m_buffer.size() - m_dataEnd;
    const qint64 requested = qMin(available, rangeRemaining);
    const qint64 bytesRead = m_file.read(m_buffer.data() + m_dataEnd, requested);
    if (bytesRead < 0) {
        m_errorString = m_file.errorString();
        return false;
    }
    if (bytesRead == 0) {
        m_eof = true;
        return false;
    }

    m_dataEnd += static_cast<int>(bytesRead);
    if (m_bufferOffset + m_dataEnd >= m_rangeEnd)
        m_eof = true;
    return true;
}

void FastTextReader::resetState(qint64 beginOffset,
                                qint64 endOffset,
                                qint64 initialLineNumber)
{
    if (m_buffer.size() != m_initialBufferSize)
        m_buffer.resize(m_initialBufferSize);
    m_dataEnd = 0;
    m_reclaimBegin = 0;
    m_bufferOffset = beginOffset;
    m_rangeBegin = beginOffset;
    m_rangeEnd = endOffset;
    m_nextLineNumber = initialLineNumber;
    m_eof = beginOffset >= endOffset;
    m_started = false;
    m_cachedNextValid = false;
    m_errorString.clear();
}
