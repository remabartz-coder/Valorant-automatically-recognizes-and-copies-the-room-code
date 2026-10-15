#include "video_decoder.h"

#include <QFileInfo>
#include <QUrl>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <memory>
#include <utility>

namespace queuepilot {
namespace {
std::int64_t clockNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct FfmpegResources {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    AVDictionary* options = nullptr;

    ~FfmpegResources() {
        av_dict_free(&options);
        sws_freeContext(scaler);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};

struct VerifiedIoPolicy {
    decltype(AVFormatContext::io_open) originalOpen = nullptr;
};

int openVerifiedIo(AVFormatContext* format, AVIOContext** stream, const char* url,
                   int flags, AVDictionary** options) {
    const auto* policy = static_cast<const VerifiedIoPolicy*>(format->opaque);
    if (!policy || !policy->originalOpen) return AVERROR(EINVAL);
    AVDictionary* temporary = nullptr;
    AVDictionary** requestOptions = options ? options : &temporary;
    const int configured = av_dict_set(requestOptions, "tls_verify", "1", 0);
    // Keep FFmpeg's protocol restrictions and inheritance for nested HLS requests.
    const int result = configured < 0 ? configured
        : policy->originalOpen(format, stream, url, flags, requestOptions);
    av_dict_free(&temporary);
    return result;
}

void notify(const VideoDecoder::StatusCallback& callback, const QString& message) noexcept {
    try {
        if (callback) {
            callback(message);
        }
    } catch (...) {
    }
}

QString withError(const QString& message, int error) {
    char description[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, description, sizeof(description));
    return message + QStringLiteral(" (FFmpeg %1: %2)").arg(error).arg(QString::fromLatin1(description));
}
}

VideoDecoder::~VideoDecoder() {
    stop();
}

void VideoDecoder::start(const DecoderOptions& options, FrameCallback frame, StatusCallback status) {
    stop();
    stopRequested_.store(false, std::memory_order_release);
    ioDeadlineNanoseconds_.store(0, std::memory_order_release);
    worker_ = std::thread([this, options, frame = std::move(frame), status = std::move(status)]() mutable {
        try {
            run(options, std::move(frame), status);
        } catch (...) {
            notify(status, QStringLiteral("Video decoding stopped after an internal error."));
        }
    });
}

void VideoDecoder::stop() {
    stopRequested_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}

void VideoDecoder::setIoDeadline(std::chrono::seconds timeout) {
    ioDeadlineNanoseconds_.store(clockNanoseconds() +
        std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count(), std::memory_order_release);
}

int VideoDecoder::interrupt(void* opaque) {
    const auto* decoder = static_cast<VideoDecoder*>(opaque);
    if (decoder->stopRequested_.load(std::memory_order_acquire)) {
        return 1;
    }
    const std::int64_t deadline = decoder->ioDeadlineNanoseconds_.load(std::memory_order_acquire);
    return deadline != 0 && clockNanoseconds() >= deadline ? 1 : 0;
}

bool VideoDecoder::waitUntil(Clock::time_point target) {
    std::unique_lock lock(wakeMutex_);
    wake_.wait_until(lock, target, [this] { return stopRequested_.load(std::memory_order_acquire); });
    return !stopRequested_.load(std::memory_order_acquire);
}

void VideoDecoder::run(DecoderOptions options, FrameCallback callback, StatusCallback status) {
    static std::once_flag initialize;
    std::call_once(initialize, [] {
        avformat_network_init();
        // Native FFmpeg diagnostics can expose signed media URLs and Cookie headers.
        av_log_set_level(AV_LOG_QUIET);
    });
    if (!options.cookies.isEmpty()) {
        notify(status, QStringLiteral("Video request headers do not accept Cookie. Use cookies only for HTTPS Douyin room-page resolution."));
        return;
    }
    const QUrl sourceUrl(options.source, QUrl::StrictMode);
    const QString localPath = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : options.source;
    const bool local = QFileInfo(localPath).isFile();
    if (!local && (!sourceUrl.isValid() || sourceUrl.host().isEmpty() ||
        (sourceUrl.scheme() != QStringLiteral("https") && sourceUrl.scheme() != QStringLiteral("http")) ||
        !sourceUrl.userInfo().isEmpty())) {
        notify(status, QStringLiteral("Video source must be an existing file or a direct HTTP(S) media address."));
        return;
    }
    if (options.referer.contains(QLatin1Char('\r')) || options.referer.contains(QLatin1Char('\n')) ||
        options.referer.contains(QChar::Null)) {
        notify(status, QStringLiteral("Video request headers contain invalid control characters."));
        return;
    }

    VerifiedIoPolicy ioPolicy;
    FfmpegResources resources;
    resources.format = avformat_alloc_context();
    if (!resources.format) {
        notify(status, QStringLiteral("Video decoder allocation failed."));
        return;
    }
    resources.format->interrupt_callback = {&VideoDecoder::interrupt, this};
    ioPolicy.originalOpen = resources.format->io_open;
    resources.format->opaque = &ioPolicy;
    resources.format->io_open = &openVerifiedIo;
    av_dict_set(&resources.options, "protocol_whitelist", local ? "file,http,https,tcp,tls,crypto" : "http,https,tcp,tls,crypto", 0);
    if (!local) {
        av_dict_set(&resources.options, "tls_verify", "1", 0);
        av_dict_set(&resources.options, "rw_timeout", "8000000", 0);
        av_dict_set(&resources.options, "reconnect", "1", 0);
        av_dict_set(&resources.options, "reconnect_streamed", "1", 0);
        av_dict_set(&resources.options, "reconnect_delay_max", "2", 0);
        av_dict_set(&resources.options, "user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36", 0);
        QByteArray headers;
        if (!options.referer.isEmpty()) {
            headers += "Referer: " + options.referer.toUtf8() + "\r\n";
        }
        if (!headers.isEmpty()) {
            av_dict_set(&resources.options, "headers", headers.constData(), 0);
        }
        if (options.lowLatency) {
            av_dict_set(&resources.options, "fflags", "nobuffer", 0);
            av_dict_set(&resources.options, "probesize", "524288", 0);
            av_dict_set(&resources.options, "analyzeduration", "1000000", 0);
        }
    }
    const QByteArray source = (local ? localPath : sourceUrl.toString(QUrl::FullyEncoded)).toUtf8();
    notify(status, QStringLiteral("Opening video source..."));
    setIoDeadline(std::chrono::seconds(12));
    const int openResult = avformat_open_input(&resources.format, source.constData(), nullptr, &resources.options);
    if (openResult < 0) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
            notify(status, withError(QStringLiteral("Could not open the video source. Its address may have expired or require valid request headers."), openResult));
        }
        return;
    }
    av_dict_free(&resources.options);
    setIoDeadline(std::chrono::seconds(12));
    const int infoResult = avformat_find_stream_info(resources.format, nullptr);
    if (infoResult < 0) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
            notify(status, withError(QStringLiteral("Could not read video stream information."), infoResult));
        }
        return;
    }
    const int videoIndex = av_find_best_stream(resources.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        notify(status, withError(QStringLiteral("The source has no supported video stream."), videoIndex));
        return;
    }
    AVStream* const stream = resources.format->streams[videoIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        notify(status, QStringLiteral("The video codec is not supported by this FFmpeg build."));
        return;
    }
    resources.codec = avcodec_alloc_context3(decoder);
    if (!resources.codec || avcodec_parameters_to_context(resources.codec, stream->codecpar) < 0) {
        notify(status, QStringLiteral("Video codec initialization failed."));
        return;
    }
    if (!local && options.lowLatency) {
        resources.codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        resources.codec->thread_count = 2;
        resources.codec->thread_type = FF_THREAD_SLICE;
    }
    const int codecOpenResult = avcodec_open2(resources.codec, decoder, nullptr);
    if (codecOpenResult < 0) {
        notify(status, withError(QStringLiteral("Video codec could not be opened."), codecOpenResult));
        return;
    }
    resources.frame = av_frame_alloc();
    resources.packet = av_packet_alloc();
    if (!resources.frame || !resources.packet) {
        notify(status, QStringLiteral("Video frame allocation failed."));
        return;
    }
    ioDeadlineNanoseconds_.store(0, std::memory_order_release);
    notify(status, QStringLiteral("Video source ready."));

    const AVRational rate = av_guess_frame_rate(resources.format, stream, nullptr);
    const double nominalStep = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;
    std::uint64_t sequence = 0;
    std::uint64_t loopFrames = 0;
    bool paceInitialized = false;
    double firstMediaSeconds = 0;
    Clock::time_point firstFrameTime;
    bool processingFailed = false;

    const auto drainFrames = [&]() -> bool {
        while (!stopRequested_.load(std::memory_order_acquire)) {
            const int result = avcodec_receive_frame(resources.codec, resources.frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return true;
            }
            if (result < 0) {
                notify(status, withError(QStringLiteral("Video frame decoding failed."), result));
                processingFailed = true;
                return false;
            }
            AVFrame* const decoded = resources.frame;
            double seconds = decoded->best_effort_timestamp == AV_NOPTS_VALUE
                ? static_cast<double>(loopFrames) * nominalStep
                : static_cast<double>(decoded->best_effort_timestamp) * av_q2d(stream->time_base);
            if (!std::isfinite(seconds)) {
                seconds = static_cast<double>(loopFrames) * nominalStep;
            }
            if (local) {
                if (!paceInitialized) {
                    paceInitialized = true;
                    firstMediaSeconds = seconds;
                    firstFrameTime = Clock::now();
                }
                const double relativeSeconds = std::max(0.0, seconds - firstMediaSeconds);
                const auto target = firstFrameTime + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(relativeSeconds));
                if (!waitUntil(target)) {
                    av_frame_unref(decoded);
                    return false;
                }
            }
            if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > 16384 || decoded->height > 16384) {
                av_frame_unref(decoded);
                notify(status, QStringLiteral("The video frame dimensions are invalid."));
                processingFailed = true;
                return false;
            }
            const auto receivedAt = Clock::now();
            QImage image(decoded->width, decoded->height, QImage::Format_RGB888);
            resources.scaler = sws_getCachedContext(resources.scaler,
                decoded->width, decoded->height, static_cast<AVPixelFormat>(decoded->format),
                decoded->width, decoded->height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            if (image.isNull() || !resources.scaler) {
                av_frame_unref(decoded);
                notify(status, QStringLiteral("Video color conversion allocation failed."));
                processingFailed = true;
                return false;
            }
            std::uint8_t* destination[] = {image.bits(), nullptr, nullptr, nullptr};
            const int destinationStride[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
            const int scaled = sws_scale(resources.scaler, decoded->data, decoded->linesize, 0,
                decoded->height, destination, destinationStride);
            av_frame_unref(decoded);
            if (scaled <= 0) {
                notify(status, QStringLiteral("Video color conversion failed."));
                processingFailed = true;
                return false;
            }
            ++loopFrames;
            if (callback) {
                callback(VideoFrame{std::move(image), ++sequence, receivedAt, seconds});
            }
        }
        return false;
    };

    while (!stopRequested_.load(std::memory_order_acquire)) {
        setIoDeadline(std::chrono::seconds(12));
        const int readResult = av_read_frame(resources.format, resources.packet);
        ioDeadlineNanoseconds_.store(0, std::memory_order_release);
        if (readResult == AVERROR(EAGAIN)) {
            if (!waitUntil(Clock::now() + std::chrono::milliseconds(5))) {
                break;
            }
            continue;
        }
        if (readResult == AVERROR_EOF) {
            avcodec_send_packet(resources.codec, nullptr);
            drainFrames();
            if (stopRequested_.load(std::memory_order_acquire) || processingFailed) {
                break;
            }
            if (local && options.loopFile) {
                if (loopFrames == 0) {
                    notify(status, QStringLiteral("The local video produced no decodable frames."));
                    break;
                }
                const std::int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
                setIoDeadline(std::chrono::seconds(5));
                const int seekResult = av_seek_frame(resources.format, videoIndex, start, AVSEEK_FLAG_BACKWARD);
                ioDeadlineNanoseconds_.store(0, std::memory_order_release);
                if (seekResult < 0) {
                    notify(status, withError(QStringLiteral("The local video could not be restarted."), seekResult));
                    break;
                }
                avcodec_flush_buffers(resources.codec);
                paceInitialized = false;
                loopFrames = 0;
                continue;
            }
            notify(status, QStringLiteral("Video source ended."));
            break;
        }
        if (readResult < 0) {
            if (!stopRequested_.load(std::memory_order_acquire)) {
                notify(status, withError(QStringLiteral("Video reading failed or timed out. Resolve a fresh media address and reconnect."), readResult));
            }
            break;
        }
        if (resources.packet->stream_index == videoIndex) {
            int sendResult = avcodec_send_packet(resources.codec, resources.packet);
            if (sendResult == AVERROR(EAGAIN)) {
                if (!drainFrames()) {
                    av_packet_unref(resources.packet);
                    break;
                }
                sendResult = avcodec_send_packet(resources.codec, resources.packet);
            }
            if (sendResult < 0 && sendResult != AVERROR_INVALIDDATA) {
                av_packet_unref(resources.packet);
                notify(status, withError(QStringLiteral("Video packet decoding failed."), sendResult));
                break;
            }
            if (!drainFrames()) {
                av_packet_unref(resources.packet);
                break;
            }
        }
        av_packet_unref(resources.packet);
    }
}

}
