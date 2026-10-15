#include "preview_widget.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

namespace queuepilot {
PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 200);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::CrossCursor);
    setToolTip(QStringLiteral("框选只包含一行房间码的区域"));
}
void PreviewWidget::setFrame(const QImage& image) {
    if (image_.cacheKey() == image.cacheKey() && image_.size() == image.size()) return;
    image_ = image;
    update();
}
void PreviewWidget::clearFrame() {
    const bool cancelled = dragging_;
    image_ = {};
    dragging_ = false;
    if (cancelled) roi_ = originalRoi_;
    update();
    if (cancelled) emit roiChanged(roi_);
}
QImage PreviewWidget::croppedFrame() const {
    if (image_.isNull()) return {};
    const QRect crop(qRound(roi_.x() * image_.width()), qRound(roi_.y() * image_.height()),
                     qRound(roi_.width() * image_.width()), qRound(roi_.height() * image_.height()));
    return image_.copy(crop.intersected(image_.rect()));
}
void PreviewWidget::setRoi(QRectF roi) {
    const QRectF bounded = roi.normalized().intersected(QRectF(0, 0, 1, 1));
    if (bounded.width() < .005 || bounded.height() < .005 || bounded == roi_) return;
    roi_ = bounded;
    update();
}
void PreviewWidget::setSelectionLocked(bool locked) {
    if (selectionLocked_ == locked) return;
    const bool cancelled = locked && dragging_;
    selectionLocked_ = locked;
    setCursor(selectionLocked_ ? Qt::ArrowCursor : Qt::CrossCursor);
    if (cancelled) {
        dragging_ = false;
        roi_ = originalRoi_;
    }
    update();
    if (cancelled) emit roiChanged(roi_);
}
QRectF PreviewWidget::imageRect() const {
    if (image_.isNull()) return {};
    const QSize scaled = image_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRectF((width() - scaled.width()) / 2.0, (height() - scaled.height()) / 2.0,
                  scaled.width(), scaled.height());
}
QPointF PreviewWidget::normalizedPoint(QPointF point) const {
    const QRectF rect = imageRect();
    return {std::clamp((point.x() - rect.x()) / rect.width(), 0.0, 1.0),
            std::clamp((point.y() - rect.y()) / rect.height(), 0.0, 1.0)};
}
void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#15191c"));
    if (image_.isNull()) {
        p.setPen(QColor("#a4aeb5"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待画面"));
        return;
    }
    const QRectF target = imageRect();
    p.drawImage(target, image_);
    const QRectF selected(target.x() + roi_.x() * target.width(),
                          target.y() + roi_.y() * target.height(),
                          roi_.width() * target.width(), roi_.height() * target.height());
    QPen selectionPen(QColor("#52d4aa"), selectionLocked_ ? 1 : 2);
    if (selectionLocked_) selectionPen.setStyle(Qt::DashLine);
    p.setPen(selectionPen);
    p.setBrush(QColor(40, 195, 145, selectionLocked_ ? 8 : 12));
    p.drawRect(selected);
}
void PreviewWidget::mousePressEvent(QMouseEvent* e) {
    if (selectionLocked_ || e->button() != Qt::LeftButton || image_.isNull() || !imageRect().contains(e->position())) return;
    dragStart_ = normalizedPoint(e->position());
    originalRoi_ = roi_;
    dragging_ = true;
    emit roiSelectionStarted();
}
void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (selectionLocked_ || !dragging_) return;
    setRoi(QRectF(dragStart_, normalizedPoint(e->position())));
}
void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (selectionLocked_ || !dragging_ || e->button() != Qt::LeftButton) return;
    dragging_ = false;
    const QRectF selection(dragStart_, normalizedPoint(e->position()));
    if (selection.normalized().width() < .005 || selection.normalized().height() < .005)
        setRoi(originalRoi_);
    else
        setRoi(selection);
    emit roiChanged(roi_);
}
}
