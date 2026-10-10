#include "util/memory.hpp"
#include <charconv>
#include <algorithm>
#include <stdexcept>
namespace iris::memory {
Pattern::Pattern(std::string_view signature) {
    while (!signature.empty()) {
        const auto start = signature.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) break;
        signature.remove_prefix(start);
        const auto end = signature.find_first_of(" \t\r\n");
        const auto token = signature.substr(0, end);
        if (token == "?" || token == "??") bytes_.emplace_back(std::nullopt);
        else {
            unsigned int value{};
            const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), value, 16);
            if (token.size() != 2 || error != std::errc{} || ptr != token.data() + token.size() || value > 255)
                throw std::invalid_argument("Invalid signature token");
            bytes_.emplace_back(static_cast<std::byte>(value));
        }
        if (end == std::string_view::npos) break;
        signature.remove_prefix(end);
    }
    if (bytes_.empty()) throw std::invalid_argument("Signature must not be empty");
    for(std::size_t i=0;i<bytes_.size();++i)if(bytes_[i])anchor_=i;
}
std::optional<std::size_t> Pattern::find(std::span<const std::byte> bytes) const noexcept {
    if (bytes_.size() > bytes.size()) return {};
    if(!anchor_)return 0;
    const auto last=bytes.size()-bytes_.size();
    // Find an exact anchor first, then verify only candidate starts. Wildcards
    // and first-match ordering retain the original scanner's semantics.
    auto candidates=bytes.subspan(*anchor_,last+1);
    std::size_t candidate_start{};
    while(!candidates.empty()) {
        const auto found=std::find(candidates.begin(),candidates.end(),*bytes_[*anchor_]);
        if(found==candidates.end())return {};
        const auto offset=candidate_start+static_cast<std::size_t>(found-candidates.begin());
        bool matches = true;
        for (std::size_t i = 0; i < bytes_.size(); ++i) {
            if (bytes_[i] && bytes[offset + i] != *bytes_[i]) { matches = false; break; }
        }
        if (matches) return offset;
        candidates=bytes.subspan(offset+*anchor_+1,last-offset);
        candidate_start=offset+1;
    }
    return {};
}

}
