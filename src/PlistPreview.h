#ifndef PLISTPREVIEW_H
#define PLISTPREVIEW_H

#include <QByteArray>

class PlistPreview
{
public:
    struct Result {
        bool valid = false;
        bool hasNestedBinaryPlists = false;
        QByteArray xml;
    };

    static bool isBinaryPlist(const QByteArray& data);
    static Result render(const QByteArray& data, bool decodeNestedBinaryPlists, int maximumDepth = 16);
};

#endif
