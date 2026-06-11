#ifndef TESTPLISTPREVIEW_H
#define TESTPLISTPREVIEW_H

#include <QObject>

class TestPlistPreview : public QObject
{
    Q_OBJECT

private slots:
    void topLevelBinaryPlistRendersXml();
    void nestedBinaryPlistCanBeDecoded();
    void nestedBinaryPlistDoesNotWrapPlistDocument();
    void recursiveDecodeOffPreservesData();
    void nsArchiveUidsRenderAsCfUidDictionaries();
    void invalidBlobIsNotPreviewable();
    void recursionDepthLimitFallsBackToData();
};

#endif
