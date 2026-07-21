#ifndef FAST_TEXT_READER_H
#define FAST_TEXT_READER_H

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstring>

// A non-owning byte slice. It is intentionally small enough to pass by value.
// Views returned by FastTextReader stay valid until the next reader operation.
class ByteView
{
public:
    Q_ALWAYS_INLINE ByteView() : m_data(""), m_size(0) {}
    Q_ALWAYS_INLINE ByteView(const char *data, int size)
        : m_data(data ? data : ""), m_size(size < 0 ? 0 : size) {}

    const char *data() const { return m_data; }
    int size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    char at(int index) const { return m_data[index]; }

    ByteView left(int count) const;
    ByteView mid(int position, int count = -1) const;
    ByteView trimmed() const;
    bool startsWith(char value) const;
    bool startsWith(ByteView value) const;

    // Character search uses memchr. The returned index is byte based.
    int find(char value, int from = 0) const;
    int find(ByteView value, int from = 0) const;

    // split() only creates ByteView objects; field bytes are never copied.
    void split(char delimiter,
               QVector<ByteView> *output,
               bool keepEmpty = true) const;

    template <typename Consumer>
    void forEachSplit(char delimiter, Consumer consumer, bool keepEmpty = true) const
    {
        int position = 0;
        for (;;) {
            const int separator = find(delimiter, position);
            const int end = separator < 0 ? m_size : separator;
            const ByteView field(m_data + position, end - position);
            if (keepEmpty || !field.isEmpty())
                consumer(field);
            if (separator < 0)
                break;
            position = separator + 1;
        }
    }

    bool toInt64(qint64 *value) const;
    bool toUInt64(quint64 *value) const;
    bool toDouble(double *value) const;
    QByteArray toByteArray() const;
    QString toUtf8() const;

private:
    const char *m_data;
    int m_size;
};

struct SourceSpan
{
    SourceSpan() : begin(0), end(0) {}
    SourceSpan(qint64 beginOffset, qint64 endOffset)
        : begin(beginOffset), end(endOffset) {}

    qint64 length() const { return end - begin; }

    qint64 begin;
    qint64 end;
};

struct SeekPoint
{
    SeekPoint(qint64 offsetValue = 0, qint64 lineNumberValue = 0)
        : offset(offsetValue), lineNumber(lineNumberValue) {}

    qint64 offset;
    // Zero means unknown. A saved LineView always has a known line number.
    qint64 lineNumber;
};

struct RegexHit
{
    RegexHit()
        : byteIndex(-1), byteLength(0), sourceOffset(-1) {}

    bool hasMatch() const { return byteIndex >= 0; }

    int byteIndex;
    int byteLength;
    qint64 sourceOffset;
    QString captured;
    QStringList captures;
};

class FieldCursor
{
public:
    FieldCursor(ByteView bytes = ByteView(),
                qint64 sourceOffset = 0,
                char delimiter = ',');

    bool next(ByteView *field);
    bool nextTrimmed(ByteView *field);
    bool readInt64(qint64 *value);
    bool readUInt64(quint64 *value);
    bool readDouble(double *value);
    bool readString(ByteView *value);
    bool readUtf8(QString *value);

    int bytePosition() const { return m_position; }
    qint64 sourcePosition() const { return m_sourceOffset + m_position; }
    SourceSpan lastSourceSpan() const { return m_lastSpan; }
    ByteView remaining() const;

private:
    void updateLastSpan(ByteView field);

    ByteView m_bytes;
    qint64 m_sourceOffset;
    char m_delimiter;
    int m_position;
    bool m_finished;
    SourceSpan m_lastSpan;
};

struct LineView
{
    LineView()
        : beginOffset(0), contentEndOffset(0), nextOffset(0),
          lineNumber(0), newlineTerminated(false) {}

    SourceSpan contentSpan() const
    {
        return SourceSpan(beginOffset, contentEndOffset);
    }
    SourceSpan sourceSpan() const
    {
        return SourceSpan(beginOffset, nextOffset);
    }
    SeekPoint seekPoint() const { return SeekPoint(beginOffset, lineNumber); }
    FieldCursor fields(char delimiter = ',') const
    {
        return FieldCursor(bytes, beginOffset, delimiter);
    }
    qint64 findAbsolute(char value, int from = 0) const;
    bool findRegex(const QRegularExpression &expression, RegexHit *hit) const;

    ByteView bytes;
    qint64 beginOffset;
    qint64 contentEndOffset;
    qint64 nextOffset;
    qint64 lineNumber;
    bool newlineTerminated;
};

// current and next point into the same stable buffer. Both remain valid while
// the current line is parsed, until nextWindow(), seek(), setRange(), or close().
struct LineWindow
{
    LineWindow() : hasNext(false) {}

    LineView current;
    LineView next;
    bool hasNext;
};

class FastTextReader
{
public:
    explicit FastTextReader(int bufferSize = 1024 * 1024);
    ~FastTextReader();

    bool open(const QString &filePath);
    void close();
    bool isOpen() const { return m_file.isOpen(); }

    // Restrict parsing to [beginOffset, endOffset). A negative endOffset means
    // EOF. initialLineNumber may be zero when an arbitrary byte seek is used.
    bool setRange(qint64 beginOffset,
                  qint64 endOffset = -1,
                  qint64 initialLineNumber = 1);
    bool seek(const SeekPoint &point, qint64 endOffset = -1);

    // Advances by one line and supplies one-line look-ahead without rescanning
    // the cached next line. Newline detection uses memchr exactly once per line.
    Q_ALWAYS_INLINE bool nextWindow(LineWindow *window);

    SeekPoint nextSeekPoint() const;
    qint64 fileSize() const { return m_fileSize; }
    qint64 rangeBegin() const { return m_rangeBegin; }
    qint64 rangeEnd() const { return m_rangeEnd; }
    QString errorString() const { return m_errorString; }
    bool hasError() const { return !m_errorString.isEmpty(); }

private:
    struct LineBounds
    {
        LineBounds()
            : start(0), contentEnd(0), next(0), lineNumber(0),
              newlineTerminated(false) {}

        int start;
        int contentEnd;
        int next;
        qint64 lineNumber;
        bool newlineTerminated;
    };

    Q_ALWAYS_INLINE bool locateLine(int start,
                                    qint64 lineNumber,
                                    LineBounds *line,
                                    LineBounds *protectedLine);
    bool fillMore(int *lineStart,
                  int *scanPosition,
                  LineBounds *protectedLine);
    Q_ALWAYS_INLINE void makeLineView(const LineBounds &bounds,
                                      LineView *view) const;
    void resetState(qint64 beginOffset,
                    qint64 endOffset,
                    qint64 initialLineNumber);

    Q_DISABLE_COPY(FastTextReader)

    QFile m_file;
    QByteArray m_buffer;
    int m_initialBufferSize;
    int m_dataEnd;
    int m_reclaimBegin;
    qint64 m_bufferOffset;
    qint64 m_fileSize;
    qint64 m_rangeBegin;
    qint64 m_rangeEnd;
    qint64 m_nextLineNumber;
    bool m_eof;
    bool m_started;
    bool m_cachedNextValid;
    LineBounds m_cachedNext;
    QString m_errorString;
};

Q_ALWAYS_INLINE bool FastTextReader::nextWindow(LineWindow *window)
{
    if (!window || !m_file.isOpen() || hasError())
        return false;

    LineBounds current;
    if (!m_started) {
        if (!locateLine(m_reclaimBegin, m_nextLineNumber, &current, 0))
            return false;
        m_started = true;
    } else {
        if (!m_cachedNextValid)
            return false;
        current = m_cachedNext;
        m_cachedNextValid = false;
    }

    m_reclaimBegin = current.start;
    LineBounds next;
    const qint64 nextLineNumber = current.lineNumber > 0
        ? current.lineNumber + 1
        : 0;
    if (locateLine(current.next, nextLineNumber, &next, &current)) {
        m_cachedNext = next;
        m_cachedNextValid = true;
    }

    m_reclaimBegin = current.start;
    makeLineView(current, &window->current);
    window->hasNext = m_cachedNextValid;
    if (window->hasNext)
        makeLineView(m_cachedNext, &window->next);
    else
        window->next = LineView();
    m_nextLineNumber = nextLineNumber;
    return true;
}

Q_ALWAYS_INLINE bool FastTextReader::locateLine(int start,
                                                qint64 lineNumber,
                                                LineBounds *line,
                                                LineBounds *protectedLine)
{
    int lineStart = start;
    int scanPosition = start;

    for (;;) {
        if (scanPosition < m_dataEnd) {
            const void *found = std::memchr(
                m_buffer.constData() + scanPosition,
                '\n',
                static_cast<size_t>(m_dataEnd - scanPosition));
            if (found) {
                const int newline = static_cast<int>(
                    static_cast<const char *>(found) - m_buffer.constData());
                int contentEnd = newline;
                if (contentEnd > lineStart &&
                    m_buffer.at(contentEnd - 1) == '\r') {
                    --contentEnd;
                }
                line->start = lineStart;
                line->contentEnd = contentEnd;
                line->next = newline + 1;
                line->lineNumber = lineNumber;
                line->newlineTerminated = true;
                return true;
            }
            scanPosition = m_dataEnd;
        }

        if (m_eof) {
            if (lineStart >= m_dataEnd)
                return false;
            line->start = lineStart;
            line->contentEnd = m_dataEnd;
            if (line->contentEnd > lineStart &&
                m_buffer.at(line->contentEnd - 1) == '\r') {
                --line->contentEnd;
            }
            line->next = m_dataEnd;
            line->lineNumber = lineNumber;
            line->newlineTerminated = false;
            return true;
        }

        if (!fillMore(&lineStart, &scanPosition, protectedLine) && hasError())
            return false;
    }
}

Q_ALWAYS_INLINE void FastTextReader::makeLineView(const LineBounds &bounds,
                                                  LineView *view) const
{
    view->bytes = ByteView(m_buffer.constData() + bounds.start,
                           bounds.contentEnd - bounds.start);
    view->beginOffset = m_bufferOffset + bounds.start;
    view->contentEndOffset = m_bufferOffset + bounds.contentEnd;
    view->nextOffset = m_bufferOffset + bounds.next;
    view->lineNumber = bounds.lineNumber;
    view->newlineTerminated = bounds.newlineTerminated;
}

#endif // FAST_TEXT_READER_H
