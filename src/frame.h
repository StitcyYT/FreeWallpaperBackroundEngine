#pragma once
#include <vector>
#include <cstdint>

struct Frame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    double pts = 0.0;
};
