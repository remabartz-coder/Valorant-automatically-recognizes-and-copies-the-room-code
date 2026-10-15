#include "stream_resolver.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <iostream>

using queuepilot::StreamResolver;

namespace {
int failures = 0;

void expect(const char* name, const QString& actual, const QString& expected) {
    if (actual != expected) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if ((arguments.size() == 3 || arguments.size() == 4) && arguments[1] == QStringLiteral("--resolve")) {
        StreamResolver liveResolver;
        int result = 2;
        QObject::connect(&liveResolver, &StreamResolver::qualitySelected, &application,
            [&](const QString& requested, const QString& actual) {
                std::cout << "Quality requested: " << requested.toStdString() << "; selected: " << actual.toStdString() << ".\n";
            });
        QObject::connect(&liveResolver, &StreamResolver::resolved, &application,
            [&](const QString& source, const QString&, const QByteArray&) {
                const QString host = QUrl(source).host();
                std::cout << "Resolved media source";
                if (!host.isEmpty()) {
                    std::cout << " (host: " << host.toStdString() << ')';
                }
                std::cout << ".\n";
                result = 0;
                application.quit();
            });
        QObject::connect(&liveResolver, &StreamResolver::failed, &application,
            [&](const QString& message) {
                std::cout << message.toStdString() << '\n';
                result = 1;
                application.quit();
            });
        QTimer::singleShot(20000, &application, [&] {
            liveResolver.cancel();
            std::cout << "Room resolution timed out.\n";
            application.quit();
        });
        liveResolver.resolve(arguments[2], {}, arguments.size() == 4 ? arguments[3] : QStringLiteral("auto"));
        application.exec();
        return result;
    }
    const QString flv = QStringLiteral("https://media.example/live.flv?token=test");
    const QString hls = QStringLiteral("https://media.example/live.m3u8");

    expect("RENDER_DATA", StreamResolver::extractMediaUrl(
        "<script id=\"RENDER_DATA\" type=\"application/json\">" +
        QJsonDocument(QJsonObject{{"room", QJsonObject{{"flv_pull_url", QJsonObject{{"FULL_HD1", flv}}}}}})
            .toJson(QJsonDocument::Compact).toPercentEncoding() + "</script>"), flv);

    expect("NEXT_DATA HLS", StreamResolver::extractMediaUrl(
        "<script id=\"__NEXT_DATA__\" type=\"application/json\">" +
        QJsonDocument(QJsonObject{{"props", QJsonObject{{"hls_pull_url", hls}}}})
            .toJson(QJsonDocument::Compact) + "</script>"), hls);

    const QJsonObject mainStreams{{"flv", flv}, {"hls", hls}};
    const QJsonObject origin{{"main", mainStreams}};
    const QJsonObject streamData{{"data", QJsonObject{{"origin", origin}}}};
    const QByteArray nestedStream = QJsonDocument(streamData).toJson(QJsonDocument::Compact);
    expect("nested JSON stream_data", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"live_core_sdk_data", QJsonObject{{"pull_data", QJsonObject{
            {"stream_data", QString::fromUtf8(nestedStream)}}}}}}).toJson(QJsonDocument::Compact)), flv);

    const QString flight = QStringLiteral("12:") + QString::fromUtf8(nestedStream) + QLatin1Char('\n');
    expect("escaped push stream", StreamResolver::extractMediaUrl(
        "<script>self.__next_f.push(" + QJsonDocument(QJsonArray{1, flight}).toJson(QJsonDocument::Compact) +
        ");</script>"), flv);

    expect("router assignment", StreamResolver::extractMediaUrl(
        "<script>window._ROUTER_DATA = " + nestedStream + ";</script>"), flv);
    QByteArray stringArray = QJsonDocument(QJsonArray{QString::fromUtf8(nestedStream)}).toJson(QJsonDocument::Compact);
    const QByteArray jsonString = stringArray.mid(1, stringArray.size() - 2);
    expect("JSON.parse string wrapper", StreamResolver::extractMediaUrl(
        "<script>window.data=JSON.parse(" + jsonString + ");</script>"), flv);

    expect("page-provided HTTP media accepted", StreamResolver::extractMediaUrl(
        R"({"flv":"http://media.example/live.flv"})"), QStringLiteral("http://media.example/live.flv"));
    expect("credentials excluded", StreamResolver::extractMediaUrl(
        R"({"flv":"https://name:secret@media.example/live.flv"})"), {});
    expect("nonmedia excluded", StreamResolver::extractMediaUrl(
        R"({"avatar":"https://media.example/avatar.jpg"})"), {});
    expect("invalid JSON excluded", StreamResolver::extractMediaUrl(
        R"(<script>{flv:'https://media.example/live.flv'}</script>)"), {});
    expect("offline excluded", StreamResolver::extractMediaUrl(
        R"(<script id="__NEXT_DATA__">{"room":{"status":4}}</script>)"), {});
    expect("JSON escape decoding", StreamResolver::extractMediaUrl(
        R"({"flv":"https:\/\/media.example\/live.flv?token=test"})"), flv);
    expect("FLV preferred", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"hls", hls}, {"flv", flv}}).toJson(QJsonDocument::Compact)), flv);
    expect("malformed page excluded", StreamResolver::extractMediaUrl(
        R"(<html><script>{"flv":"https://media.example/live.flv"</script></html>)"), {});
    expect("unstructured URL excluded", StreamResolver::extractMediaUrl(
        "<html>https://media.example/live.flv</html>"), {});
    expect("oversize page excluded", StreamResolver::extractMediaUrl(QByteArray(8 * 1024 * 1024 + 1, 'x')), {});

    const QString originUrl = QStringLiteral("https://media.example/original.flv");
    const QString uhdUrl = QStringLiteral("https://media.example/ultra.flv");
    const QString hdUrl = QStringLiteral("https://media.example/high.flv");
    const QString sdUrl = QStringLiteral("https://media.example/standard.flv");
    const QString ldUrl = QStringLiteral("https://media.example/low.flv");
    const QJsonObject qualities{
        {"origin", QJsonObject{{"main", QJsonObject{{"flv", originUrl}}}}},
        {"uhd", QJsonObject{{"main", QJsonObject{{"flv", uhdUrl}}}}},
        {"hd", QJsonObject{{"main", QJsonObject{{"flv", hdUrl}}}}},
        {"sd", QJsonObject{{"main", QJsonObject{{"flv", sdUrl}}}}},
        {"ld", QJsonObject{{"main", QJsonObject{{"flv", ldUrl}}}}}
    };
    const QByteArray qualityDocument = QJsonDocument(QJsonObject{{"stream_data", qualities}}).toJson(QJsonDocument::Compact);
    expect("auto picks highest FLV", StreamResolver::extractMediaUrl(qualityDocument), originUrl);
    expect("origin preference", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("origin")), originUrl);
    expect("uhd preference", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("uhd")), uhdUrl);
    expect("hd preference", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("hd")), hdUrl);
    expect("sd preference", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("sd")), sdUrl);
    expect("ld preference", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("ld")), ldUrl);
    expect("invalid quality defaults to auto", StreamResolver::extractMediaUrl(qualityDocument, QStringLiteral("unknown")), originUrl);
    QJsonObject missingHd = qualities;
    missingHd.remove(QStringLiteral("hd"));
    expect("missing quality falls back to nearest lower", StreamResolver::extractMediaUrl(
        QJsonDocument(missingHd).toJson(QJsonDocument::Compact), QStringLiteral("hd")), sdUrl);
    const QByteArray legacyQualityDocument = QJsonDocument(QJsonObject{{"flv_pull_url", QJsonObject{
        {"FULL_HD1", originUrl}, {"HD1", uhdUrl}, {"SD1", hdUrl}, {"SD2", sdUrl}, {"LD1", ldUrl}}}})
        .toJson(QJsonDocument::Compact);
    expect("legacy original preference", StreamResolver::extractMediaUrl(legacyQualityDocument, QStringLiteral("origin")), originUrl);
    expect("legacy ultra preference", StreamResolver::extractMediaUrl(legacyQualityDocument, QStringLiteral("uhd")), uhdUrl);
    expect("legacy high preference", StreamResolver::extractMediaUrl(legacyQualityDocument, QStringLiteral("hd")), hdUrl);
    expect("legacy standard preference", StreamResolver::extractMediaUrl(legacyQualityDocument, QStringLiteral("sd")), sdUrl);
    expect("legacy low preference", StreamResolver::extractMediaUrl(legacyQualityDocument, QStringLiteral("ld")), ldUrl);
    expect("exact quality HLS beats different quality FLV", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"origin", QJsonObject{{"flv", originUrl}}}, {"sd", QJsonObject{{"hls", hls}}}})
            .toJson(QJsonDocument::Compact), QStringLiteral("sd")), hls);
    expect("nearest explicit key overrides container origin", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"origin", qualities}}).toJson(QJsonDocument::Compact), QStringLiteral("hd")), hdUrl);
    expect("unknown quality field falls back without inference", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"flv_pull_url", QJsonObject{{"CUSTOM", flv}}}}).toJson(QJsonDocument::Compact),
        QStringLiteral("ld")), flv);
    const QString httpOrigin = QStringLiteral("http://media.example/original.flv");
    const QString httpHd = QStringLiteral("http://media.example/high.flv");
    expect("auto recognizes HTTP origin above unknown HTTPS md", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"origin", QJsonObject{{"flv", httpOrigin}}},
            {"md", QJsonObject{{"flv", flv}}}}).toJson(QJsonDocument::Compact)), httpOrigin);
    expect("requested HTTP quality beats different HTTPS quality", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"origin", QJsonObject{{"flv", originUrl}}},
            {"hd", QJsonObject{{"flv", httpHd}}}}).toJson(QJsonDocument::Compact), QStringLiteral("hd")), httpHd);
    expect("same quality prefers HTTPS above HTTP main", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"hd", QJsonObject{{"main", QJsonObject{{"flv", httpHd}}},
            {"backup", QJsonObject{{"flv", hdUrl}}}}}}).toJson(QJsonDocument::Compact), QStringLiteral("hd")), hdUrl);
    expect("equal-distance lower fallback outranks HTTPS FLV", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"uhd", QJsonObject{{"flv", uhdUrl}}},
            {"sd", QJsonObject{{"hls", QStringLiteral("http://media.example/standard.m3u8")}}}})
            .toJson(QJsonDocument::Compact), QStringLiteral("hd")), QStringLiteral("http://media.example/standard.m3u8"));
    expect("same quality FLV precedes HTTPS HLS", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"hd", QJsonObject{{"flv", httpHd}, {"hls", hls}}}})
            .toJson(QJsonDocument::Compact), QStringLiteral("hd")), httpHd);
    expect("auto FLV precedes higher-quality HLS", StreamResolver::extractMediaUrl(
        QJsonDocument(QJsonObject{{"origin", QJsonObject{{"hls", hls}}},
            {"ld", QJsonObject{{"flv", ldUrl}}}}).toJson(QJsonDocument::Compact)), ldUrl);
    expect("HTTP credentials excluded", StreamResolver::extractMediaUrl(
        R"({"flv":"http://name:secret@media.example/live.flv"})"), {});
    expect("non HTTP(S) media excluded", StreamResolver::extractMediaUrl(
        R"({"flv":"rtmp://media.example/live.flv"})"), {});

    StreamResolver resolver;
    QString resolvedSource;
    QByteArray resolvedCookies;
    QString failureMessage;
    int resolutionCount = 0;
    QString requestedQuality;
    QString actualQuality;
    QObject::connect(&resolver, &StreamResolver::qualitySelected, &application,
        [&](const QString& requested, const QString& actual) {
            requestedQuality = requested;
            actualQuality = actual;
        });
    QObject::connect(&resolver, &StreamResolver::resolved, &application,
        [&](const QString& source, const QString&, const QByteArray& cookies) {
            resolvedSource = source;
            resolvedCookies = cookies;
            ++resolutionCount;
        });
    QObject::connect(&resolver, &StreamResolver::failed, &application,
        [&](const QString& message) { failureMessage = message; });
    resolver.resolve(QStringLiteral("https://media.example/old.flv"));
    resolver.resolve(flv, {}, QStringLiteral("sd"));
    QCoreApplication::processEvents();
    expect("cancel superseded source", resolvedSource, flv);
    expect("direct media preserves requested quality", requestedQuality, QStringLiteral("sd"));
    expect("direct media quality remains unknown", actualQuality, QStringLiteral("auto"));
    expect("direct media has no Cookie", QString::fromLatin1(resolvedCookies), {});
    if (resolutionCount != 1) {
        std::cerr << "FAIL: superseded callback count\n";
        ++failures;
    }
    for (const QString& media : {flv, hls, QStringLiteral("http://media.example/test.mp4")}) {
        failureMessage.clear();
        const int previousCount = resolutionCount;
        resolver.resolve(media, QByteArrayLiteral("queuepilot_audit_dummy=1"));
        QCoreApplication::processEvents();
        if (resolutionCount != previousCount || !failureMessage.contains(QStringLiteral("Clear Cookie"))) {
            std::cerr << "FAIL: direct media Cookie was not explicitly rejected\n";
            ++failures;
        }
    }
    for (const QByteArray& cookie : {QByteArray("name=a\r\nbad=value"), QByteArray("name=a\0bad", 10)}) {
        failureMessage.clear();
        resolver.resolve(QStringLiteral("https://live.douyin.com/12345678901"), cookie);
        QCoreApplication::processEvents();
        if (!failureMessage.contains(QStringLiteral("single Cookie"))) {
            std::cerr << "FAIL: unsafe room-page Cookie was not rejected before requesting\n";
            ++failures;
        }
    }

    QTemporaryFile video;
    if (!video.open()) {
        std::cerr << "FAIL: temporary file\n";
        ++failures;
    } else {
        resolver.resolve(video.fileName());
        QCoreApplication::processEvents();
        expect("local file bypass", resolvedSource, QFileInfo(video.fileName()).absoluteFilePath());
    }

    if (failures == 0) {
        std::cout << "Resolver fixture tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
