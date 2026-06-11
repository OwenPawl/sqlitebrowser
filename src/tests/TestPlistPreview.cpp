#include "TestPlistPreview.h"
#include "../PlistPreview.h"

#include <QtTest/QTest>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

QTEST_APPLESS_MAIN(TestPlistPreview)

#ifdef Q_OS_MACOS

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

QByteArray toBinaryPlist(CFPropertyListRef plist)
{
    CFErrorRef rawError = nullptr;
    ScopedCF<CFDataRef> data(CFPropertyListCreateData(kCFAllocatorDefault,
                                                      plist,
                                                      kCFPropertyListBinaryFormat_v1_0,
                                                      0,
                                                      &rawError));
    ScopedCF<CFErrorRef> error(rawError);
    Q_UNUSED(error);

    if (!data.get())
        return QByteArray();

    return QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data.get())),
                      static_cast<int>(CFDataGetLength(data.get())));
}

ScopedCF<CFStringRef> cfString(const char* value)
{
    return ScopedCF<CFStringRef>(CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8));
}

QByteArray plistWithStringValue(const char* key, const char* value)
{
    ScopedCF<CFStringRef> cfKey(cfString(key).release());
    ScopedCF<CFStringRef> cfValue(cfString(value).release());
    const void* keys[] = {cfKey.get()};
    const void* values[] = {cfValue.get()};
    ScopedCF<CFDictionaryRef> dict(CFDictionaryCreate(kCFAllocatorDefault,
                                                      keys,
                                                      values,
                                                      1,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    return toBinaryPlist(dict.get());
}

QByteArray plistWithDataValue(const char* key, const QByteArray& value)
{
    ScopedCF<CFStringRef> cfKey(cfString(key).release());
    ScopedCF<CFDataRef> cfValue(CFDataCreate(kCFAllocatorDefault,
                                             reinterpret_cast<const UInt8*>(value.constData()),
                                             value.size()));
    const void* keys[] = {cfKey.get()};
    const void* values[] = {cfValue.get()};
    ScopedCF<CFDictionaryRef> dict(CFDictionaryCreate(kCFAllocatorDefault,
                                                      keys,
                                                      values,
                                                      1,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    return toBinaryPlist(dict.get());
}

QByteArray nestedPlist(int depth)
{
    QByteArray value = plistWithStringValue("leaf", "done");
    for (int i = 0; i < depth; ++i)
        value = plistWithDataValue("nested", value);
    return value;
}

} // namespace

#endif

void TestPlistPreview::topLevelBinaryPlistRendersXml()
{
#ifdef Q_OS_MACOS
    PlistPreview::Result result = PlistPreview::render(plistWithStringValue("name", "preview"), true);
    QVERIFY(result.valid);
    QVERIFY(!result.hasNestedBinaryPlists);
    QVERIFY(result.xml.contains("<plist version=\"1.0\">"));
    QVERIFY(result.xml.contains("<key>name</key>"));
    QVERIFY(result.xml.contains("<string>preview</string>"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}

void TestPlistPreview::nestedBinaryPlistCanBeDecoded()
{
#ifdef Q_OS_MACOS
    QByteArray nested = plistWithStringValue("inner", "value");
    PlistPreview::Result result = PlistPreview::render(plistWithDataValue("payload", nested), true);
    QVERIFY(result.valid);
    QVERIFY(result.hasNestedBinaryPlists);
    QVERIFY(result.xml.contains("<bplist>"));
    QVERIFY(result.xml.contains("</bplist>"));
    QVERIFY(result.xml.contains("<key>inner</key>"));
    QVERIFY(result.xml.contains("<string>value</string>"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}

void TestPlistPreview::nestedBinaryPlistDoesNotWrapPlistDocument()
{
#ifdef Q_OS_MACOS
    QByteArray nested = plistWithStringValue("inner", "value");
    PlistPreview::Result result = PlistPreview::render(plistWithDataValue("payload", nested), true);
    QVERIFY(result.valid);
    QVERIFY(result.hasNestedBinaryPlists);
    QVERIFY(!result.xml.contains("<bplist>\n\t\t<plist version=\"1.0\">"));
    QVERIFY(result.xml.contains("<bplist>\n\t\t\t<dict>"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}

void TestPlistPreview::recursiveDecodeOffPreservesData()
{
#ifdef Q_OS_MACOS
    QByteArray nested = plistWithStringValue("inner", "value");
    PlistPreview::Result result = PlistPreview::render(plistWithDataValue("payload", nested), false);
    QVERIFY(result.valid);
    QVERIFY(result.hasNestedBinaryPlists);
    QVERIFY(!result.xml.contains("<bplist>"));
    QVERIFY(result.xml.contains("<data>"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}

void TestPlistPreview::nsArchiveUidsRenderAsCfUidDictionaries()
{
#ifdef Q_OS_MACOS
    QByteArray archive = QByteArray::fromHex(
        "62706c6973743030d401020304050613165924617263686976657258246f626a656374735424746f70582476657273696f6e"
        "5f100f4e534b657965644172636869766572a307080d55246e756c6cd2090a0b0c5624636c617373546e616d6580025464"
        "656d6fd20e0f10115824636c61737365735a24636c6173736e616d65a211125444656d6f584e534f626a656374d1141554"
        "726f6f74800112000186a008111b24293244484e535a5f61666b747f82879093989a000000000000010100000000000000"
        "170000000000000000000000000000009f");

    PlistPreview::Result result = PlistPreview::render(archive, true);
    QVERIFY(result.valid);
    QVERIFY(result.xml.contains("<key>CF$UID</key>"));
    QVERIFY(result.xml.contains("<integer>1</integer>"));
    QVERIFY(result.xml.contains("<integer>2</integer>"));
    QVERIFY(!result.xml.contains("Unsupported plist value"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}

void TestPlistPreview::invalidBlobIsNotPreviewable()
{
    PlistPreview::Result result = PlistPreview::render(QByteArray("not a plist"), true);
    QVERIFY(!result.valid);
    QVERIFY(result.xml.isEmpty());
}

void TestPlistPreview::recursionDepthLimitFallsBackToData()
{
#ifdef Q_OS_MACOS
    PlistPreview::Result result = PlistPreview::render(nestedPlist(3), true, 3);
    QVERIFY(result.valid);
    QVERIFY(result.hasNestedBinaryPlists);
    QVERIFY(result.xml.contains("<bplist>"));
    QVERIFY(result.xml.contains("<data>"));
#else
    QSKIP("Binary plist preview is macOS-only");
#endif
}
