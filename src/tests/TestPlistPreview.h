#ifndef TESTPLISTPREVIEW_H
#define TESTPLISTPREVIEW_H

#include <QObject>

class TestPlistPreview : public QObject
{
    Q_OBJECT

private slots:
    void topLevelBinaryPlistRendersXml();
    void nestedBinaryPlistCanBeDecoded();
    void recursiveDecodeOffPreservesData();
    void invalidBlobIsNotPreviewable();
    void recursionDepthLimitFallsBackToData();
};

#endif
