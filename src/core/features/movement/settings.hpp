#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
namespace iris::movement {
struct Options {
    float chance{100},attempt_limit{};
    float turn_speed{1440};
    bool subtick{},slow_walk{},quick_stop{};
    bool operator==(const Options&) const=default;
};
struct Number {const char* key;const char* label;float Options::*member;float low,high;const char* format;};
inline constexpr Number numbers[]{
    {"chance","Bounce chance",&Options::chance,0,100,"%.0f%%"},
    {"attempt_limit","Bounce limit (0 = unlimited)",&Options::attempt_limit,0,100,"%.0f"},
    {"turn_speed","Air steering turn speed",&Options::turn_speed,0,2880,"%.0f deg/s"}};
struct Toggle {const char* key;bool Options::*member;};
inline constexpr Toggle toggles[]{{"subtick",&Options::subtick},{"slow_walk",&Options::slow_walk},{"quick_stop",&Options::quick_stop}};
inline void validate(const Options& s) {
    for(const auto& n:numbers)if(!std::isfinite(s.*n.member)||s.*n.member<n.low||s.*n.member>n.high)throw std::invalid_argument("Movement setting is outside its supported range.");
    if(s.attempt_limit!=std::floor(s.attempt_limit))throw std::invalid_argument("Bounce limit must be a whole number.");
}
}
