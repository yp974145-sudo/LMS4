#include "model.hpp"
#include <cassert>
#include <iostream>
int main() {
    lesson::Order order;
    order.customer="Alice";order.cargo="Books";order.quantity=3;
    assert(!lesson::validate(order));
    order.quantity=-5;
    assert(lesson::validate(order));
    std::cout << "PASS: valid order accepted; negative quantity rejected\n";
}
