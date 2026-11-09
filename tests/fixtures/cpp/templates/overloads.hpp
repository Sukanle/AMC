#pragma once

class Overloaded {
public:
    Overloaded() = default;

    int pick(int value) const { return value; }
    double pick(double value) const { return value; }

    int tag() const { return 7; }
};
