#pragma once
#include <QWidget>
#include <QImage>
#include <QRectF>

namespace queuepilot {
class PreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& image);
    void clearFrame();
    QImage croppedFrame() const;
    QRectF roi() const { return roi_; }
    void setRoi(QRectF roi);
    void setSelectionLocked(bool locked);
    bool selectionLocked() const { return selectionLocked_; }
signals:
    void roiSelectionStarted();
    void roiChanged(QRectF roi);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF imageRect() const;
    QPointF normalizedPoint(QPointF point) const;
    QImage image_;
    QRectF roi_{0.35, 0.38, 0.30, 0.14};
    QPointF dragStart_;
    QRectF originalRoi_;
    bool dragging_ = false;
    bool selectionLocked_ = false;
};
}
