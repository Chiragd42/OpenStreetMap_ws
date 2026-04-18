#include "metrics.hpp"

namespace osm {

void Stopwatch::start() {
    start_time_ = std::chrono::steady_clock::now();
    started_ = true;
}

double Stopwatch::elapsed_seconds() const {
    if (!started_) {
        return 0.0;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - start_time_);
    return elapsed.count();
}

} // namespace osm
