#include "speech_notifier.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

using namespace queuepilot;
using namespace std::chrono_literals;

namespace {
struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread::id caller = std::this_thread::get_id();
    bool wrongThread = false;
    bool complete = false;
    bool blockStart = false;
    bool releaseStart = false;
    int starts = 0;
    int polls = 0;
    int cancels = 0;
    int destroyed = 0;
};
class FakeSpeaker final : public SpeechNotifier::Speaker {
public:
    explicit FakeSpeaker(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~FakeSpeaker() override
    {
        std::lock_guard lock(state_->mutex);
        checkThread();
        ++state_->destroyed;
        state_->changed.notify_all();
    }
    bool start() override
    {
        std::unique_lock lock(state_->mutex);
        checkThread();
        ++state_->starts;
        state_->changed.notify_all();
        state_->changed.wait(lock, [this] { return !state_->blockStart || state_->releaseStart; });
        return true;
    }
    bool finished() override
    {
        std::lock_guard lock(state_->mutex);
        checkThread();
        ++state_->polls;
        state_->changed.notify_all();
        return state_->complete;
    }
    void cancel() override
    {
        std::lock_guard lock(state_->mutex);
        checkThread();
        ++state_->cancels;
        state_->changed.notify_all();
    }
private:
    void checkThread() { state_->wrongThread |= std::this_thread::get_id() == state_->caller; }
    std::shared_ptr<State> state_;
};
template<class Predicate>
bool waitFor(const std::shared_ptr<State>& state, Predicate predicate)
{
    std::unique_lock lock(state->mutex);
    return state->changed.wait_for(lock, 2s, predicate);
}
bool require(bool result, const char* message)
{
    if (!result) std::cerr << message << '\n';
    return result;
}
}

int main()
{
    const auto state = std::make_shared<State>();
    {
        SpeechNotifier notifier([state] { return std::make_unique<FakeSpeaker>(state); });
        notifier.sayRecognitionSucceeded();
        if (!require(waitFor(state, [&] { return state->starts == 1; }), "speech did not start")) return 1;
        for (int i = 0; i < 100; ++i) notifier.sayRecognitionSucceeded();
        if (!require(waitFor(state, [&] { return state->polls >= 2; }), "speech was not polled")) return 1;
        {
            std::lock_guard lock(state->mutex);
            if (!require(state->starts == 1, "repeated notifications queued speech")) return 1;
        }
        notifier.setEnabled(false);
        if (!require(waitFor(state, [&] { return state->cancels >= 1; }), "disable did not cancel speech")) return 1;
        for (int i = 0; i < 100; ++i) notifier.sayRecognitionSucceeded();
        notifier.setEnabled(true);
        notifier.sayRecognitionSucceeded();
        if (!require(waitFor(state, [&] { return state->starts == 2; }), "speech did not resume")) return 1;
    }
    {
        std::lock_guard lock(state->mutex);
        if (!require(state->starts == 2, "disabled requests were replayed")) return 1;
        if (!require(state->cancels >= 2, "destruction did not cancel speech")) return 1;
        if (!require(state->destroyed == 1 && !state->wrongThread, "speaker lifetime escaped its worker thread")) return 1;
    }
    const auto completed = std::make_shared<State>();
    {
        SpeechNotifier notifier([completed] { return std::make_unique<FakeSpeaker>(completed); });
        notifier.sayRecognitionSucceeded();
        if (!require(waitFor(completed, [&] { return completed->starts == 1; }), "completion test did not start")) return 1;
        for (int i = 0; i < 100; ++i) notifier.sayRecognitionSucceeded();
        int polls = 0;
        {
            std::lock_guard lock(completed->mutex);
            completed->complete = true;
            polls = completed->polls;
        }
        if (!require(waitFor(completed, [&] { return completed->polls > polls; }), "completion was not observed")) return 1;
        {
            std::unique_lock lock(completed->mutex);
            if (!require(!completed->changed.wait_for(lock, 100ms, [&] { return completed->starts > 1; }),
                         "repeated requests survived completion")) return 1;
        }
        notifier.sayRecognitionSucceeded();
        if (!require(waitFor(completed, [&] { return completed->starts == 2; }), "new notification was lost after completion")) return 1;
    }
    const auto blocked = std::make_shared<State>();
    blocked->blockStart = true;
    {
        SpeechNotifier notifier([blocked] { return std::make_unique<FakeSpeaker>(blocked); });
        notifier.sayRecognitionSucceeded();
        const bool started = waitFor(blocked, [&] { return blocked->starts == 1; });
        notifier.setEnabled(false);
        notifier.sayRecognitionSucceeded();
        {
            std::lock_guard lock(blocked->mutex);
            blocked->releaseStart = true;
            blocked->changed.notify_all();
        }
        if (!require(started, "blocking speaker did not start")) return 1;
        if (!require(waitFor(blocked, [&] { return blocked->cancels >= 1; }), "blocked start ignored disable")) return 1;
    }
    {
        SpeechNotifier unavailable([] { return std::unique_ptr<SpeechNotifier::Speaker>(); });
        unavailable.sayRecognitionSucceeded();
        unavailable.setEnabled(false);
    }
    std::cout << "speech lifecycle passed without audio output\n";
    return 0;
}
