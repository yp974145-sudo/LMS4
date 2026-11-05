#include "service.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto repo=std::make_shared<lesson::MemoryOrderRepository>();
    lesson::OrderService service(repo);
    lesson::User customer{"customer",lesson::Role::Customer};
    lesson::User admin{"admin",lesson::Role::Admin};
    lesson::User storage{"storage",lesson::Role::Storage};
    lesson::Order input;
    input.customer="Alice";input.cargo="Books";input.quantity=3;
    input.creator="spoofed-admin";
    assert(!service.create(input,storage).ok);
    auto created=service.create(input,customer);
    assert(created.ok && created.order.creator=="customer");
    assert(service.find(created.order.id)->quantity==3);
    assert(!service.approve(created.order.id,customer));
    assert(service.approve(created.order.id,admin));
    assert(!service.approve(created.order.id,admin));
    assert(!service.find("missing"));
    std::cout << "PASS: create/query/authorization/identity/approval/duplicate checks\n";
}
