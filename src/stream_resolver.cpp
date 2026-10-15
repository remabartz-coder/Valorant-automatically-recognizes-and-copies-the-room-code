#include "stream_resolver.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>

namespace queuepilot {
namespace {
constexpr qsizetype maximumPageBytes = 8 * 1024 * 1024;
constexpr int maximumParsedDocuments = 256;
constexpr int maximumNesting = 48;

bool isDouyinHost(const QString& host) {
    const QString lowered = host.toLower();
    return lowered == QStringLiteral("douyin.com") || lowered.endsWith(QStringLiteral(".douyin.com"));
}

bool isDirectMedia(const QUrl& url) {
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    return suffix == QStringLiteral("flv") || suffix == QStringLiteral("m3u8") ||
           suffix == QStringLiteral("mp4") || suffix == QStringLiteral("mkv") ||
           suffix == QStringLiteral("webm") || suffix == QStringLiteral("ts") ||
           suffix == QStringLiteral("mov");
}

QString normalizedInput(QString input) {
    input = input.trimmed();
    input.replace(QStringLiteral("https\\://"), QStringLiteral("https://"));
    input.replace(QStringLiteral("http\\://"), QStringLiteral("http://"));
    return input;
}

struct Candidate {
    QString url;
    QString quality = QStringLiteral("auto");
    bool flv = false;
    bool secure = false;
    int contextPreference = 0;
};

QString normalizedQuality(QString quality) {
    quality = quality.trimmed().toLower();
    if (quality == QStringLiteral("origin") || quality == QStringLiteral("uhd") ||
        quality == QStringLiteral("hd") || quality == QStringLiteral("sd") || quality == QStringLiteral("ld")) {
        return quality;
    }
    return QStringLiteral("auto");
}

int qualityRank(const QString& quality) {
    if (quality == QStringLiteral("origin")) return 5;
    if (quality == QStringLiteral("uhd")) return 4;
    if (quality == QStringLiteral("hd")) return 3;
    if (quality == QStringLiteral("sd")) return 2;
    if (quality == QStringLiteral("ld")) return 1;
    return 0;
}

QString qualityFromPath(const QString& path) {
    const QStringList keys = path.toLower().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // Legacy pull maps and SDK stream_data use distinct names for the same quality ladder.
    for (auto it = keys.crbegin(); it != keys.crend(); ++it) {
        const QString& key = *it;
        if (key == QStringLiteral("origin") || key == QStringLiteral("original") || key == QStringLiteral("full_hd1")) {
            return QStringLiteral("origin");
        }
        if (key == QStringLiteral("uhd") || key == QStringLiteral("hd1")) return QStringLiteral("uhd");
        if (key == QStringLiteral("hd") || key == QStringLiteral("sd1")) return QStringLiteral("hd");
        if (key == QStringLiteral("sd") || key == QStringLiteral("sd2")) return QStringLiteral("sd");
        if (key == QStringLiteral("ld") || key == QStringLiteral("ld1")) return QStringLiteral("ld");
    }
    return QStringLiteral("auto");
}

class EmbeddedJsonReader {
public:
    explicit EmbeddedJsonReader(QString quality = QStringLiteral("auto"))
        : requestedQuality_(normalizedQuality(std::move(quality))) {}

    Candidate extract(const QByteArray& html) {
        if (html.size() > maximumPageBytes) {
            return {};
        }
        const QString source = QString::fromUtf8(html);
        parse(source, {}, 0);
        const QRegularExpression scripts(
            QStringLiteral("<script\\b[^>]*>([\\s\\S]*?)</script\\s*>"),
            QRegularExpression::CaseInsensitiveOption);
        QVector<QString> bodies;
        auto matches = scripts.globalMatch(source);
        while (matches.hasNext() && remainingDocuments_ > 0) {
            const auto match = matches.next();
            const QString body = match.captured(1).trimmed();
            bodies.push_back(body);
            parse(body, {}, 0);
            if (body.contains(QLatin1Char('%'))) {
                const QString decoded = QString::fromUtf8(QByteArray::fromPercentEncoding(body.toUtf8()));
                if (decoded != body) {
                    parse(decoded, {}, 0);
                }
            }
        }
        // Parse application/json scripts first so unrelated JavaScript cannot consume the budget.
        for (const QString& body : bodies) {
            if (remainingDocuments_ <= 0) {
                break;
            }
            if (body.contains(QStringLiteral("flv"), Qt::CaseInsensitive) ||
                body.contains(QStringLiteral("hls"), Qt::CaseInsensitive) ||
                body.contains(QStringLiteral("__next_f")) || body.contains(QStringLiteral("__pace_f"))) {
                const QRegularExpression jsonParse(
                    QStringLiteral("JSON\\.parse\\s*\\(\\s*(\"(?:\\\\.|[^\"\\\\])*\")"));
                auto literals = jsonParse.globalMatch(body);
                while (literals.hasNext() && remainingDocuments_ > 0) {
                    const QString literal = literals.next().captured(1);
                    parse(QLatin1Char('[') + literal + QLatin1Char(']'), {}, 0);
                }
                scan(body, {}, 0);
            }
        }
        return best_;
    }

private:
    bool preferred(const Candidate& candidate, const Candidate& current) const {
        const int requestedRank = qualityRank(requestedQuality_);
        const int candidateRank = qualityRank(candidate.quality);
        const int currentRank = qualityRank(current.quality);
        if (requestedRank > 0) {
            if ((candidateRank > 0) != (currentRank > 0)) return candidateRank > 0;
            if (candidateRank > 0) {
                const int candidateDistance = std::abs(candidateRank - requestedRank);
                const int currentDistance = std::abs(currentRank - requestedRank);
                if (candidateDistance != currentDistance) return candidateDistance < currentDistance;
                if ((candidateRank > requestedRank) != (currentRank > requestedRank)) {
                    return candidateRank <= requestedRank;
                }
            }
        } else {
            if (candidate.flv != current.flv) return candidate.flv;
            if (candidateRank != currentRank) return candidateRank > currentRank;
        }
        if (candidate.flv != current.flv) return candidate.flv;
        if (candidate.secure != current.secure) return candidate.secure;
        return candidate.contextPreference > current.contextPreference;
    }

    void consider(QString text, const QString& path) {
        text = text.trimmed();
        if (text.startsWith(QStringLiteral("https%3A"), Qt::CaseInsensitive) ||
            text.startsWith(QStringLiteral("http%3A"), Qt::CaseInsensitive)) {
            text = QString::fromUtf8(QByteArray::fromPercentEncoding(text.toUtf8()));
        }
        const QUrl url(text, QUrl::StrictMode);
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")) ||
            url.host().isEmpty() || !url.userInfo().isEmpty()) {
            return;
        }
        const QString loweredPath = url.path().toLower();
        const QString loweredKey = path.toLower();
        const bool flv = loweredPath.endsWith(QStringLiteral(".flv")) || loweredKey.contains(QStringLiteral("flv"));
        const bool hls = loweredPath.endsWith(QStringLiteral(".m3u8")) || loweredKey.contains(QStringLiteral("hls"));
        if (!flv && !hls) {
            return;
        }
        int contextPreference = 0;
        if (loweredKey.contains(QStringLiteral("stream")) || loweredKey.contains(QStringLiteral("pull"))) {
            contextPreference += 10;
        }
        if (loweredKey.contains(QStringLiteral("main"))) {
            contextPreference += 5;
        }
        const Candidate candidate{url.toString(QUrl::FullyEncoded), qualityFromPath(path), flv,
            scheme == QStringLiteral("https"), contextPreference};
        if (best_.url.isEmpty() || preferred(candidate, best_)) {
            best_ = candidate;
        }
    }

    void walk(const QJsonValue& value, const QString& path, int depth) {
        if (depth > maximumNesting || remainingDocuments_ <= 0) {
            return;
        }
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                walk(it.value(), path + QLatin1Char('/') + it.key(), depth + 1);
            }
        } else if (value.isArray()) {
            const QJsonArray array = value.toArray();
            for (const auto& item : array) {
                walk(item, path, depth + 1);
            }
        } else if (value.isString()) {
            const QString text = value.toString();
            consider(text, path);
            const QString trimmed = text.trimmed();
            if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
                parse(trimmed, path, depth + 1);
            } else if (trimmed.startsWith(QStringLiteral("%7B"), Qt::CaseInsensitive) ||
                       trimmed.startsWith(QStringLiteral("%5B"), Qt::CaseInsensitive)) {
                parse(QString::fromUtf8(QByteArray::fromPercentEncoding(trimmed.toUtf8())), path, depth + 1);
            } else if (trimmed.contains(QStringLiteral("{\"")) || trimmed.contains(QStringLiteral("[\""))) {
                scan(trimmed, path, depth + 1);
            }
        }
    }

    bool parse(const QString& text, const QString& path, int depth) {
        if (remainingDocuments_ <= 0 || depth > maximumNesting) {
            return false;
        }
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || (trimmed.front() != QLatin1Char('{') && trimmed.front() != QLatin1Char('['))) {
            return false;
        }
        --remainingDocuments_;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || document.isNull()) {
            return false;
        }
        walk(document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array()), path, depth + 1);
        return true;
    }

    void scan(const QString& source, const QString& path, int depth) {
        if (depth > maximumNesting || remainingDocuments_ <= 0) {
            return;
        }
        // Isolate JSON containers from script wrappers, then let Qt parse their contents.
        qsizetype start = -1;
        int nesting = 0;
        bool inString = false;
        bool escaped = false;
        for (qsizetype i = 0; i < source.size() && remainingDocuments_ > 0; ++i) {
            const QChar ch = source[i];
            if (start < 0) {
                if (ch == QLatin1Char('{') || ch == QLatin1Char('[')) {
                    start = i;
                    nesting = 1;
                    inString = false;
                    escaped = false;
                }
                continue;
            }
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (ch == QLatin1Char('\\')) {
                    escaped = true;
                } else if (ch == QLatin1Char('"')) {
                    inString = false;
                }
                continue;
            }
            if (ch == QLatin1Char('"')) {
                inString = true;
            } else if (ch == QLatin1Char('{') || ch == QLatin1Char('[')) {
                ++nesting;
            } else if (ch == QLatin1Char('}') || ch == QLatin1Char(']')) {
                --nesting;
                if (nesting == 0) {
                    parse(source.mid(start, i - start + 1), path, depth + 1);
                    start = -1;
                }
            }
        }
    }

    int remainingDocuments_ = maximumParsedDocuments;
    QString requestedQuality_;
    Candidate best_;
};
}

StreamResolver::StreamResolver(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {}

StreamResolver::~StreamResolver() {
    cancel();
}

QString StreamResolver::extractMediaUrl(const QByteArray& html, QString quality) {
    return EmbeddedJsonReader{std::move(quality)}.extract(html).url;
}

void StreamResolver::cancel() {
    ++generation_;
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_.clear();
    }
    response_.clear();
    cookies_.clear();
    referer_.clear();
}

void StreamResolver::resolve(QString input, QByteArray cookies, QString quality) {
    cancel();
    input = normalizedInput(std::move(input));
    quality = normalizedQuality(std::move(quality));
    const quint64 generation = generation_;
    const auto deferFailure = [this, generation](QString message) {
        QTimer::singleShot(0, this, [this, generation, message = std::move(message)] {
            if (generation == generation_) {
                emit failed(message);
            }
        });
    };
    const QUrl url(input, QUrl::StrictMode);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : input;
    const QFileInfo local(localPath);
    if (local.exists() && local.isFile()) {
        QTimer::singleShot(0, this, [this, generation, path = local.absoluteFilePath(), quality] {
            if (generation == generation_) {
                emit qualitySelected(quality, QStringLiteral("auto"));
                if (generation == generation_) emit resolved(path, {}, {});
            }
        });
        return;
    }
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) ||
        !url.userInfo().isEmpty()) {
        deferFailure(QStringLiteral("Enter a Douyin live room URL, a direct HTTP(S) media URL, or an existing local video file."));
        return;
    }
    if (cookies.contains('\r') || cookies.contains('\n') || cookies.contains('\0')) {
        deferFailure(QStringLiteral("Cookies must be a single Cookie header value without line breaks or NUL characters."));
        return;
    }
    if (isDirectMedia(url)) {
        if (!cookies.isEmpty()) {
            deferFailure(QStringLiteral("Cookies are accepted only for HTTPS Douyin room pages. Clear Cookie before using a direct media URL."));
            return;
        }
        QTimer::singleShot(0, this, [this, generation, source = url.toString(QUrl::FullyEncoded), quality] {
            if (generation == generation_) {
                emit qualitySelected(quality, QStringLiteral("auto"));
                if (generation == generation_) emit resolved(source, {}, {});
            }
        });
        return;
    }
    if (url.scheme() != QStringLiteral("https") || !isDouyinHost(url.host())) {
        deferFailure(QStringLiteral("Room-page resolution requires a HTTPS Douyin URL. Direct media URLs can be used separately."));
        return;
    }

    QUrl pageUrl = url;
    const QString webRoomId = QUrlQuery(pageUrl).queryItemValue(QStringLiteral("live_web_rid"));
    if (pageUrl.host().compare(QStringLiteral("live.douyin.com"), Qt::CaseInsensitive) == 0 &&
        QRegularExpression(QStringLiteral("^[0-9]{1,24}$")).match(webRoomId).hasMatch()) {
        pageUrl.setPath(QLatin1Char('/') + webRoomId);
        pageUrl.setQuery(QString{});
        pageUrl.setFragment(QString{});
    }
    cookies_ = std::move(cookies);
    referer_ = pageUrl.toString(QUrl::FullyEncoded);
    QNetworkRequest request(pageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setTransferTimeout(12000);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8");
    if (!cookies_.isEmpty()) {
        request.setRawHeader("Cookie", cookies_);
    }
    reply_ = network_->get(request);
    QNetworkReply* const activeReply = reply_;
    connect(activeReply, &QNetworkReply::redirected, this, [this, activeReply, generation](const QUrl& redirected) {
        if (generation != generation_ || reply_ != activeReply) {
            return;
        }
        if (redirected.scheme() == QStringLiteral("https") && isDouyinHost(redirected.host()) &&
            redirected.userInfo().isEmpty()) {
            activeReply->redirectAllowed();
        } else {
            activeReply->abort();
        }
    });
    connect(activeReply, &QIODevice::readyRead, this, [this, activeReply, generation] {
        if (generation != generation_ || reply_ != activeReply) {
            return;
        }
        response_ += activeReply->read(maximumPageBytes + 1 - response_.size());
        if (response_.size() > maximumPageBytes) {
            activeReply->abort();
        }
    });
    connect(activeReply, &QNetworkReply::finished, this, [this, activeReply, generation, quality] {
        if (generation != generation_ || reply_ != activeReply) {
            activeReply->deleteLater();
            return;
        }
        const bool networkFailed = activeReply->error() != QNetworkReply::NoError;
        if (response_.size() <= maximumPageBytes) {
            response_ += activeReply->read(maximumPageBytes + 1 - response_.size());
        }
        const Candidate selection = networkFailed ? Candidate{} : EmbeddedJsonReader{quality}.extract(response_);
        const QString media = selection.url;
        const QString referer = referer_;
        response_.clear();
        cookies_.clear();
        referer_.clear();
        reply_.clear();
        activeReply->deleteLater();
        if (!media.isEmpty()) {
            emit qualitySelected(quality, selection.quality);
            if (generation == generation_) emit resolved(media, referer, {});
        } else if (networkFailed) {
            emit failed(QStringLiteral("The live page could not be loaded. Check the connection or supply a current direct media URL."));
        } else {
            emit failed(QStringLiteral("No accessible FLV/HLS address was found. The room may be offline, require login, or use a changed page format. A current direct media URL can be supplied manually."));
        }
    });
    QTimer::singleShot(15000, activeReply, [this, activeReply, generation] {
        if (generation == generation_ && reply_ == activeReply && !activeReply->isFinished()) {
            activeReply->abort();
        }
    });
}

}
