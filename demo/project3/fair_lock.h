#include <memory>
#include <mutex>
#include <list>

class FairLock {
public:
    void lock() {
        auto waiter = std::make_shared<std::mutex>();
        waiter->lock();
        {
            std::lock_guard<std::mutex> lg(_mtx);
            if (0 == _active_cnt++) return;
            _waiters.push_back(waiter);
        }
        waiter->lock();
    }

    void unlock() {
        std::shared_ptr<std::mutex> waiter;
        {
            std::lock_guard<std::mutex> lg(_mtx);
            if (0 == --_active_cnt) return;
            waiter = _waiters.front();
            _waiters.pop_front();
        }
        waiter->unlock();
    }

private:
    std::mutex _mtx;
    uint64_t _active_cnt = 0;
    std::list<std::shared_ptr<std::mutex>> _waiters;
};
