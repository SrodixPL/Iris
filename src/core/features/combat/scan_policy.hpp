#pragma once
#include <chrono>
namespace iris::combat {
struct QueryBudget {
    using Clock=std::chrono::steady_clock;
    Clock::time_point started;
    unsigned limit{},queries{};
    explicit QueryBudget(unsigned maximum,Clock::time_point now=Clock::now()) noexcept:started(now),limit(maximum){}
    bool take(Clock::time_point now=Clock::now()) noexcept {
        if(now<started||now-started>std::chrono::milliseconds(2)||queries>=limit)return false;
        ++queries;return true;
    }
};
}
