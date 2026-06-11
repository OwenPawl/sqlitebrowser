#include "PlistPreview.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

bool PlistPreview::isBinaryPlist(const QByteArray& data)
{
    return data.startsWith("bplist00");
}

#ifndef Q_OS_MACOS

PlistPreview::Result PlistPreview::render(const QByteArray& data, bool decodeNestedBinaryPlists, int maximumDepth)
{
    Q_UNUSED(data);
    Q_UNUSED(decodeNestedBinaryPlists);
    Q_UNUSED(maximumDepth);
    return Result();
}

#else

namespace {

struct CFReleaser {
    void operator()(const void* value) const
    {
        if (value)
            CFRelease(value);
    }
};

template<typename T>
class ScopedCF
{
public:
    explicit ScopedCF(T value = nullptr) : m_value(value) {}
    ~ScopedCF() { CFReleaser()(m_value); }

    ScopedCF(const ScopedCF&) = delete;
    ScopedCF& operator=(const ScopedCF&) = delete;

    ScopedCF(ScopedCF&& other) noexcept : m_value(other.release()) {}
    ScopedCF& operator=(ScopedCF&& other) noexcept
    {
        if (this != &other) {
            CFReleaser()(m_value);
            m_value = other.release();
        }
        return *this;
    }

    T get() const { return m_value; }
    T release()
    {
        T value = m_value;
        m_value = nullptr;
        return value;
    }

private:
    T m_value;
};

QByteArray cfDataToByteArray(CFDataRef data)
{
    if (!data)
        return QByteArray();

    return QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                      static_cast<int>(CFDataGetLength(data)));
}

ScopedCF<CFPropertyListRef> parseBinaryPlist(const QByteArray& data)
{
    if (!PlistPreview::isBinaryPlist(data))
        return ScopedCF<CFPropertyListRef>();

    ScopedCF<CFDataRef> cfData(CFDataCreate(kCFAllocatorDefault,
                                            reinterpret_cast<const UInt8*>(data.constData()),
                                            static_cast<CFIndex>(data.size())));
    if (!cfData.get())
        return ScopedCF<CFPropertyListRef>();

    CFErrorRef rawError = nullptr;
    CFPropertyListFormat format = kCFPropertyListBinaryFormat_v1_0;
    CFPropertyListRef plist = CFPropertyListCreateWithData(kCFAllocatorDefault,
                                                           cfData.get(),
                                                           kCFPropertyListImmutable,
                                                           &format,
                                                           &rawError);
    ScopedCF<CFErrorRef> error(rawError);
    Q_UNUSED(error);

    return ScopedCF<CFPropertyListRef>(plist);
}

QByteArray standardXml(CFPropertyListRef plist)
{
    if (!plist)
        return QByteArray();

    CFErrorRef rawError = nullptr;
    ScopedCF<CFDataRef> xml(CFPropertyListCreateData(kCFAllocatorDefault,
                                                     plist,
                                                     kCFPropertyListXMLFormat_v1_0,
                                                     0,
                                                     &rawError));
    ScopedCF<CFErrorRef> error(rawError);
    Q_UNUSED(error);

    return cfDataToByteArray(xml.get());
}

bool cfDataLooksLikeNestedBinaryPlist(CFDataRef data)
{
    return PlistPreview::isBinaryPlist(cfDataToByteArray(data));
}

bool containsNestedBinaryPlist(CFTypeRef value)
{
    if (!value)
        return false;

    CFTypeID type = CFGetTypeID(value);
    if (type == CFDataGetTypeID())
        return cfDataLooksLikeNestedBinaryPlist(static_cast<CFDataRef>(value));

    if (type == CFArrayGetTypeID()) {
        CFArrayRef array = static_cast<CFArrayRef>(value);
        CFIndex count = CFArrayGetCount(array);
        for (CFIndex i = 0; i < count; ++i) {
            if (containsNestedBinaryPlist(CFArrayGetValueAtIndex(array, i)))
                return true;
        }
        return false;
    }

    if (type == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = static_cast<CFDictionaryRef>(value);
        CFIndex count = CFDictionaryGetCount(dict);
        QVector<const void*> keys(count);
        QVector<const void*> values(count);
        CFDictionaryGetKeysAndValues(dict, keys.data(), values.data());
        for (CFIndex i = 0; i < count; ++i) {
            if (containsNestedBinaryPlist(values.at(i)))
                return true;
        }
    }

    return false;
}

QString cfStringToQString(CFStringRef value)
{
    if (!value)
        return QString();

    CFIndex length = CFStringGetLength(value);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(static_cast<int>(maxSize), 0);
    if (!CFStringGetCString(value, buffer.data(), maxSize, kCFStringEncodingUTF8))
        return QString();

    return QString::fromUtf8(buffer.constData());
}

QString xmlEscaped(QString value)
{
    value.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    value.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    value.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return value;
}

QString indent(int depth)
{
    return QString(depth, QLatin1Char('\t'));
}

void appendBase64Data(QString& out, CFDataRef data, int depth)
{
    QByteArray base64 = cfDataToByteArray(data).toBase64();
    out += indent(depth) + QLatin1String("<data>\n");

    const int lineLength = 68;
    for (int offset = 0; offset < base64.size(); offset += lineLength) {
        out += indent(depth + 1);
        out += QString::fromLatin1(base64.mid(offset, lineLength));
        out += QLatin1Char('\n');
    }

    out += indent(depth) + QLatin1String("</data>\n");
}

void appendValue(QString& out, CFTypeRef value, int depth, bool decodeNestedBinaryPlists, int maximumDepth);

void appendDocument(QString& out, CFTypeRef value, int depth, bool decodeNestedBinaryPlists, int maximumDepth)
{
    out += indent(depth) + QLatin1String("<plist version=\"1.0\">\n");
    appendValue(out, value, depth + 1, decodeNestedBinaryPlists, maximumDepth);
    out += indent(depth) + QLatin1String("</plist>\n");
}

void appendArray(QString& out, CFArrayRef array, int depth, bool decodeNestedBinaryPlists, int maximumDepth)
{
    out += indent(depth) + QLatin1String("<array>\n");
    CFIndex count = CFArrayGetCount(array);
    for (CFIndex i = 0; i < count; ++i)
        appendValue(out, CFArrayGetValueAtIndex(array, i), depth + 1, decodeNestedBinaryPlists, maximumDepth);
    out += indent(depth) + QLatin1String("</array>\n");
}

void appendDictionary(QString& out, CFDictionaryRef dict, int depth, bool decodeNestedBinaryPlists, int maximumDepth)
{
    out += indent(depth) + QLatin1String("<dict>\n");

    CFIndex count = CFDictionaryGetCount(dict);
    QVector<const void*> keys(count);
    QVector<const void*> values(count);
    CFDictionaryGetKeysAndValues(dict, keys.data(), values.data());

    struct Entry {
        QString key;
        const void* value;
    };
    QVector<Entry> entries;
    entries.reserve(static_cast<int>(count));

    for (CFIndex i = 0; i < count; ++i) {
        QString key;
        if (CFGetTypeID(keys.at(i)) == CFStringGetTypeID())
            key = cfStringToQString(static_cast<CFStringRef>(keys.at(i)));
        else
            key = QString::fromLatin1("<non-string key>");
        entries.push_back({key, values.at(i)});
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
        return lhs.key < rhs.key;
    });

    for (const Entry& entry : entries) {
        out += indent(depth + 1) + QLatin1String("<key>") + xmlEscaped(entry.key) + QLatin1String("</key>\n");
        appendValue(out, entry.value, depth + 1, decodeNestedBinaryPlists, maximumDepth);
    }

    out += indent(depth) + QLatin1String("</dict>\n");
}

void appendData(QString& out, CFDataRef data, int depth, bool decodeNestedBinaryPlists, int maximumDepth)
{
    QByteArray bytes = cfDataToByteArray(data);
    if (!decodeNestedBinaryPlists || depth >= maximumDepth || !PlistPreview::isBinaryPlist(bytes)) {
        appendBase64Data(out, data, depth);
        return;
    }

    ScopedCF<CFPropertyListRef> nested(parseBinaryPlist(bytes));
    if (!nested.get()) {
        appendBase64Data(out, data, depth);
        return;
    }

    out += indent(depth) + QLatin1String("<bplist>\n");
    appendDocument(out, nested.get(), depth + 1, decodeNestedBinaryPlists, maximumDepth);
    out += indent(depth) + QLatin1String("</bplist>\n");
}

void appendNumber(QString& out, CFNumberRef number, int depth)
{
    if (CFNumberIsFloatType(number)) {
        double value = 0.0;
        CFNumberGetValue(number, kCFNumberDoubleType, &value);
        out += indent(depth) + QLatin1String("<real>") + QString::number(value, 'g', 16) + QLatin1String("</real>\n");
        return;
    }

    long long value = 0;
    CFNumberGetValue(number, kCFNumberLongLongType, &value);
    out += indent(depth) + QLatin1String("<integer>") + QString::number(value) + QLatin1String("</integer>\n");
}

void appendDate(QString& out, CFDateRef date, int depth)
{
    constexpr qint64 cfAbsoluteTimeUnixEpochOffset = 978307200;
    qint64 unixSeconds = cfAbsoluteTimeUnixEpochOffset + static_cast<qint64>(CFDateGetAbsoluteTime(date));
    QString value = QDateTime::fromSecsSinceEpoch(unixSeconds, Qt::UTC).toString(Qt::ISODate);
    out += indent(depth) + QLatin1String("<date>") + value + QLatin1String("</date>\n");
}

void appendValue(QString& out, CFTypeRef value, int depth, bool decodeNestedBinaryPlists, int maximumDepth)
{
    if (!value) {
        out += indent(depth) + QLatin1String("<string></string>\n");
        return;
    }

    CFTypeID type = CFGetTypeID(value);
    if (type == CFDictionaryGetTypeID()) {
        appendDictionary(out, static_cast<CFDictionaryRef>(value), depth, decodeNestedBinaryPlists, maximumDepth);
    } else if (type == CFArrayGetTypeID()) {
        appendArray(out, static_cast<CFArrayRef>(value), depth, decodeNestedBinaryPlists, maximumDepth);
    } else if (type == CFStringGetTypeID()) {
        out += indent(depth) + QLatin1String("<string>") + xmlEscaped(cfStringToQString(static_cast<CFStringRef>(value))) + QLatin1String("</string>\n");
    } else if (type == CFDataGetTypeID()) {
        appendData(out, static_cast<CFDataRef>(value), depth, decodeNestedBinaryPlists, maximumDepth);
    } else if (type == CFBooleanGetTypeID()) {
        out += indent(depth) + (CFBooleanGetValue(static_cast<CFBooleanRef>(value)) ? QLatin1String("<true/>\n") : QLatin1String("<false/>\n"));
    } else if (type == CFNumberGetTypeID()) {
        appendNumber(out, static_cast<CFNumberRef>(value), depth);
    } else if (type == CFDateGetTypeID()) {
        appendDate(out, static_cast<CFDateRef>(value), depth);
    } else {
        out += indent(depth) + QLatin1String("<string>Unsupported plist value</string>\n");
    }
}

QByteArray recursiveXml(CFPropertyListRef plist, int maximumDepth)
{
    QString out;
    out += QLatin1String("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    out += QLatin1String("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    appendDocument(out, plist, 0, true, maximumDepth);
    return out.toUtf8();
}

} // namespace

PlistPreview::Result PlistPreview::render(const QByteArray& data, bool decodeNestedBinaryPlists, int maximumDepth)
{
    Result result;

    ScopedCF<CFPropertyListRef> plist(parseBinaryPlist(data));
    if (!plist.get())
        return result;

    result.valid = true;
    result.hasNestedBinaryPlists = containsNestedBinaryPlist(plist.get());

    if (decodeNestedBinaryPlists && result.hasNestedBinaryPlists)
        result.xml = recursiveXml(plist.get(), maximumDepth);
    else
        result.xml = standardXml(plist.get());

    return result;
}

#endif
