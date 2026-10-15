#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace queuepilot {

class StreamResolver final : public QObject {
    Q_OBJECT

public:
    explicit StreamResolver(QObject* parent = nullptr);
    ~StreamResolver() override;

    void resolve(QString input, QByteArray cookies = {}, QString quality = QStringLiteral("auto"));
    void cancel();

    static QString extractMediaUrl(const QByteArray& html, QString quality = QStringLiteral("auto"));

signals:
    void resolved(QString mediaUrl, QString referer, QByteArray cookies);
    void qualitySelected(QString requested, QString actual);
    void failed(QString message);

private:
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QByteArray response_;
    QByteArray cookies_;
    QString referer_;
    quint64 generation_ = 0;
};

}
