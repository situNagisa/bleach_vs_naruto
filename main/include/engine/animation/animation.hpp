#pragma once
#include <string>
#include <vector>
#include "frame.hpp"

class Animation {
public:
    std::string name;
    std::vector<Frame> frames;
};
