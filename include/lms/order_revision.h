#pragma once
#include "lms/models.h"
#include "lms/crypto.h"
#include <sstream>
#include <locale>

namespace lms {
// Content revision: no change to existing file or SQL schema. Length prefixes
// and exact hexadecimal floating point prevent ambiguous snapshot encodings.
inline std::string order_revision(const Order& o) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (const auto* s : {&o.order_id,&o.customer_name,&o.customer_phone,
            &o.customer_address,&o.cargo_name,&o.sender_address,&o.receiver_address,
            &o.reject_reason,&o.creator,&o.create_time,&o.auditor,&o.audit_time})
        out << s->size() << ':' << *s;
    out << '|' << int(o.cargo_type) << '|' << o.cargo_quantity << '|'
        << int(o.status) << '|' << std::hexfloat << o.cargo_weight << '|' << o.cargo_volume;
    return crypto::sha256(out.str());
}
}
