#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace queuepilot {
class SpeechNotifier final {
public:
    class Speaker {
    public:
        virtual ~Speaker() = default;
        virtual bool start() = 0;
        virtual bool finished() = 0;
        virtual void cancel() = 0;
    };
    using SpeakerFactory = std::function<std::unique_ptr<Speaker>()>;

    SpeechNotifier();
    explicit SpeechNotifier(SpeakerFactory createSpeaker);
    ~SpeechNotifier();
    SpeechNotifier(const SpeechNotifier&) = delete;
    SpeechNotifier& operator=(const SpeechNotifier&) = delete;

    void sayRecognitionSucceeded();
    void setEnabled(bool enabled);

private:
    void run(SpeakerFactory createSpeaker);
    std::mutex mutex_;
    std::condition_variable changed_;
    bool enabled_ = true;
    bool stopping_ = false;
    bool pending_ = false;
    bool speaking_ = false;
    unsigned long long revision_ = 0;
    unsigned long long speakingRevision_ = 0;
    std::thread thread_;
};
}
