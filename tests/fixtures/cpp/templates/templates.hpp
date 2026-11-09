#pragma once

#include <stddef.h>

template<typename T>
struct Box {
    T value;
    T get() const { return value; }
    void set(T next) { value = next; }
};

template struct Box<int>;
template struct Box<float>;

template<typename T>
T add(T a, T b) {
    return a + b;
}

template int add<int>(int, int);
template float add<float>(float, float);

template<>
const char *add<const char *>(const char *a, const char *b) {
    (void)b;
    return a;
}

template<typename T>
concept Numeric = requires(T value) { value + value; };

template<Numeric T>
T multiply(T a, T b) {
    return a * b;
}

template int multiply<int>(int, int);
