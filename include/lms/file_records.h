#pragma once
#include "lms/file_storage.h"
#include "lms/models.h"
#include <sstream>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace lms::file_records {
using file_storage::CorruptData;
inline void read_exact(std::istream& in, char* bytes, std::size_t size) {
    if (size && !in.read(bytes,static_cast<std::streamsize>(size))) throw CorruptData("Truncated storage record");
}
template<class T> inline T read_value(std::istream& in) {
    T value{}; read_exact(in,reinterpret_cast<char*>(&value),sizeof(value)); return value;
}
inline std::string read_string(std::istream& in) {
    const auto length=read_value<int>(in);
    if (length < 0 || length > 255) throw CorruptData("Invalid storage string length");
    std::string value(static_cast<std::size_t>(length),'\0');
    read_exact(in,value.data(),value.size());
    return value;
}
template<class T> inline void write_value(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value),sizeof(value));
}
inline void write_string(std::ostream& out, const std::string& value) {
    if (value.size() > 255) throw file_storage::StorageError("Storage string exceeds 255 bytes");
    write_value(out,static_cast<int>(value.size()));
    out.write(value.data(),static_cast<std::streamsize>(value.size()));
}
inline bool more(std::istream& in) {
    const auto next=in.peek();
    if (in.bad()) throw file_storage::StorageError("Cannot read storage record");
    return next != std::char_traits<char>::eof();
}
inline std::vector<User> decode_users(const std::string& body) {
    std::istringstream in(body,std::ios::binary | std::ios::in);
    std::vector<User> result;
    std::unordered_set<std::string> names;
    std::unordered_set<int> ids;
    while (more(in)) {
        User u;
        u.id=read_value<int>(in);
        u.username=read_string(in); u.password_hash=read_string(in);
        const auto role=read_value<std::uint8_t>(in), status=read_value<std::uint8_t>(in);
        if (u.id <= 0 || u.username.empty() || u.password_hash.empty() || role > 3 || status > 1 ||
            !names.insert(u.username).second || !ids.insert(u.id).second)
            throw CorruptData("Invalid or duplicate user record");
        u.role=static_cast<UserRole>(role); u.status=static_cast<UserStatus>(status);
        result.push_back(std::move(u));
    }
    if (result.empty()) throw CorruptData("User snapshot contains no accounts");
    return result;
}
inline std::string encode_users(const std::vector<User>& users) {
    std::ostringstream out(std::ios::binary | std::ios::out);
    for (const auto& u:users) {
        write_value(out,u.id); write_string(out,u.username); write_string(out,u.password_hash);
        write_value(out,static_cast<std::uint8_t>(u.role)); write_value(out,static_cast<std::uint8_t>(u.status));
    }
    const auto body=out.str();
    decode_users(body); // Do not persist records that our own reader would reject.
    return body;
}
inline std::vector<Order> decode_orders(const std::string& body) {
    std::istringstream in(body,std::ios::binary | std::ios::in);
    std::vector<Order> result;
    std::unordered_set<std::string> ids;
    while (more(in)) {
        Order o;
        o.order_id=read_string(in); o.customer_name=read_string(in);
        o.customer_phone=read_string(in); o.customer_address=read_string(in);
        o.cargo_name=read_string(in);
        const auto cargo=read_value<std::uint8_t>(in);
        o.cargo_weight=read_value<double>(in); o.cargo_quantity=read_value<int>(in); o.cargo_volume=read_value<double>(in);
        o.sender_address=read_string(in); o.receiver_address=read_string(in);
        const auto status=read_value<std::uint8_t>(in);
        o.reject_reason=read_string(in); o.creator=read_string(in); o.create_time=read_string(in);
        o.auditor=read_string(in); o.audit_time=read_string(in);
        if (o.order_id.empty() || o.customer_name.empty() || o.cargo_name.empty() || cargo > 3 || status > 6 ||
            !std::isfinite(o.cargo_weight) || !std::isfinite(o.cargo_volume) || o.cargo_weight < 0 ||
            o.cargo_volume < 0 || o.cargo_quantity < 1 || !ids.insert(o.order_id).second)
            throw CorruptData("Invalid or duplicate order record");
        o.cargo_type=static_cast<CargoType>(cargo); o.status=static_cast<OrderStatus>(status);
        result.push_back(std::move(o));
    }
    return result;
}
inline std::string encode_orders(const std::vector<Order>& orders) {
    std::ostringstream out(std::ios::binary | std::ios::out);
    for (const auto& o:orders) {
        write_string(out,o.order_id); write_string(out,o.customer_name);
        write_string(out,o.customer_phone); write_string(out,o.customer_address); write_string(out,o.cargo_name);
        write_value(out,static_cast<std::uint8_t>(o.cargo_type));
        write_value(out,o.cargo_weight); write_value(out,o.cargo_quantity); write_value(out,o.cargo_volume);
        write_string(out,o.sender_address); write_string(out,o.receiver_address);
        write_value(out,static_cast<std::uint8_t>(o.status));
        write_string(out,o.reject_reason); write_string(out,o.creator); write_string(out,o.create_time);
        write_string(out,o.auditor); write_string(out,o.audit_time);
    }
    const auto body=out.str(); decode_orders(body); return body;
}
inline std::vector<User> read_users(const std::filesystem::path& file) {
    return decode_users(file_storage::payload('U',file_storage::read_bytes(file)));
}
inline std::vector<Order> read_orders(const std::filesystem::path& file) {
    return decode_orders(file_storage::payload('O',file_storage::read_bytes(file)));
}
inline void check(const std::filesystem::path& file, const std::string& kind) {
    if (kind == "users") read_users(file);
    else if (kind == "orders") read_orders(file);
    else throw std::invalid_argument("Kind must be users or orders");
}
inline std::filesystem::path restore(const std::filesystem::path& file, const std::string& kind) {
    const auto backup=file_storage::backup_path(file);
    check(backup,kind); // Never replace anything until the backup is fully validated.
    const auto bytes=file_storage::read_bytes(backup);
    std::filesystem::path damaged;
    if (file_storage::exists(file)) {
        damaged=file_storage::unique_sibling(file,"damaged");
        file_storage::write_new(damaged,file_storage::read_bytes(file));
    }
    // Keep the validated .bak intact; save() would overwrite it with damaged data.
    file_storage::atomic_replace(file,bytes);
    return damaged;
}
} // namespace lms::file_records
