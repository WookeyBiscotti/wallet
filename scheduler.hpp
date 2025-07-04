#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include <absl/time/clock.h>
#include <absl/time/time.h>

class Scheduler {
public:
    using Callback = std::function<void(absl::Time time)>;

    Scheduler() {
        _isRunning = true;
        _thread = std::thread([&]() {
            std::unique_lock lk(_mutex);

            while (_isRunning) {
                std::vector<CallbackData> needExecute;
                auto now = absl::Now();
                for (auto& cb : _callbacks) {
                    if (cb.first <= now) {
                        needExecute.push_back(cb.second);
                    }
                }

                lk.unlock();
                for (const auto& cb : needExecute) {
                    cb.cb(cb.tp);
                }
                lk.lock();

                for (const auto& cb : needExecute) {
                    if (cb.repeatInterval == absl::Duration{}) {
                        removeTask(cb.id);
                    } else {
                        increaseTaskTpIfExist(cb.id, cb.repeatInterval);
                    }
                }

                if (_callbacks.empty()) {
                    _cond.wait(lk);
                } else {
                    now = absl::Now();
                    auto delta = _callbacks.begin()->first - now;
                    _cond.wait_for(lk, absl::ToChronoMicroseconds(delta));
                }
            }
        });
    }

    ~Scheduler() {
        std::unique_lock lk(_mutex);
        _isRunning = false;
        _cond.notify_all();
    }

    void removeTask(std::size_t id) {
        std::unique_lock lk(_mutex);
        removeTask(id, lk);
    }

    std::size_t schedule(absl::Time tp, Callback cb, absl::Duration repeatInterval = absl::Duration{}) {
        std::unique_lock lk(_mutex);
        _callbacks.emplace(tp, CallbackData{_nextId, tp, std::move(cb)});

        _cond.notify_one();
        return _nextId++;
    }

private:
    void increaseTaskTpIfExist(std::size_t id, absl::Duration repeatInterval) {
        for (auto it = _callbacks.begin(); it != _callbacks.end(); ++it) {
            if (it->second.id == id) {
                auto cb = std::move(it->second);
                _callbacks.erase(it);
                _callbacks.emplace(cb.tp + cb.repeatInterval, std::move(cb));

                return;
            }
        }
    }

    void removeTask(std::size_t id, const std::unique_lock<std::mutex>& lk) {
        for (auto it = _callbacks.begin(); it != _callbacks.end(); ++it) {
            if (it->second.id == id) {
                _callbacks.erase(it);
                return;
            }
        }
    }

private:
    std::mutex _mutex;
    std::thread _thread;
    std::condition_variable _cond;
    std::size_t _nextId = 0;
    bool _isRunning;
    struct CallbackData {
        std::size_t id;
        absl::Time tp;
        Callback cb;
        absl::Duration repeatInterval;
    };
    std::multimap<absl::Time, CallbackData> _callbacks;
};
