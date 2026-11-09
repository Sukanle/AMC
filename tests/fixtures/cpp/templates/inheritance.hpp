#pragma once

struct BaseA {
    int a;
};

struct BaseB {
    int b;
};

struct Derived : BaseA, BaseB {
    int c;
};
