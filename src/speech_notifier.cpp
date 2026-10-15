#include "speech_notifier.h"
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#include <chrono>
#include <utility>

namespace queuepilot {
namespace {
class SapiSpeaker final : public SpeechNotifier::Speaker {
public:
    SapiSpeaker()
    {
        initialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        if (initialized_) {
            CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&voice_));
            if (voice_) {
                Microsoft::WRL::ComPtr<ISpObjectTokenCategory> category;
                Microsoft::WRL::ComPtr<IEnumSpObjectTokens> voices;
                Microsoft::WRL::ComPtr<ISpObjectToken> chineseVoice;
                if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&category))) &&
                    SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)) &&
                    SUCCEEDED(category->EnumTokens(L"Language=804", nullptr, &voices)) &&
                    voices->Next(1, &chineseVoice, nullptr) == S_OK)
                    voice_->SetVoice(chineseVoice.Get());
            }
        }
    }
    ~SapiSpeaker() override
    {
        if (voice_) {
            cancel();
            voice_->Release();
        }
        if (initialized_) CoUninitialize();
    }
    bool start() override
    {
        return voice_ && SUCCEEDED(voice_->Speak(L"\u8bc6\u522b\u6210\u529f",
            SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr));
    }
    bool finished() override
    {
        if (!voice_) return true;
        const HRESULT result = voice_->WaitUntilDone(0);
        return result != S_FALSE;
    }
    void cancel() override
    {
        if (voice_) voice_->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    }
private:
    ISpVoice* voice_ = nullptr;
    bool initialized_ = false;
};
}

SpeechNotifier::SpeechNotifier()
    : SpeechNotifier([] { return std::make_unique<SapiSpeaker>(); }) {}

SpeechNotifier::SpeechNotifier(SpeakerFactory createSpeaker)
    : thread_([this, factory = std::move(createSpeaker)] { run(factory); }) {}

SpeechNotifier::~SpeechNotifier()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_ = false;
    }
    changed_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void SpeechNotifier::sayRecognitionSucceeded()
{
    {
        std::lock_guard lock(mutex_);
        if (!enabled_ || stopping_ || pending_ || (speaking_ && speakingRevision_ == revision_)) return;
        pending_ = true;
    }
    changed_.notify_one();
}

void SpeechNotifier::setEnabled(bool enabled)
{
    {
        std::lock_guard lock(mutex_);
        if (enabled_ == enabled) return;
        enabled_ = enabled;
        if (!enabled) {
            pending_ = false;
            ++revision_;
        }
    }
    changed_.notify_one();
}

void SpeechNotifier::run(SpeakerFactory createSpeaker)
{
    std::unique_ptr<Speaker> speaker;
    try {
        if (createSpeaker) speaker = createSpeaker();
    } catch (...) {
        return;
    }
    if (!speaker) return;
    try {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            changed_.wait(lock, [this] { return stopping_ || (enabled_ && pending_); });
            if (stopping_) break;
            pending_ = false;
            const auto revision = revision_;
            speakingRevision_ = revision;
            speaking_ = true;
            lock.unlock();
            bool active = speaker->start();
            lock.lock();
            while (active && !stopping_ && enabled_ && revision == revision_) {
                lock.unlock();
                active = !speaker->finished();
                lock.lock();
                if (!active) break;
                changed_.wait_for(lock, std::chrono::milliseconds(10), [this, revision] {
                    return stopping_ || !enabled_ || revision != revision_;
                });
            }
            lock.unlock();
            if (active) speaker->cancel();
            lock.lock();
            speaking_ = false;
        }
        lock.unlock();
        speaker->cancel();
    } catch (...) {
        std::lock_guard lock(mutex_);
        speaking_ = false;
        pending_ = false;
    }
}
}
