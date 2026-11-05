#pragma once
#include <stdexcept>
namespace lms {
struct RepositoryError : std::runtime_error { using std::runtime_error::runtime_error; };
struct DatabaseError : RepositoryError { using RepositoryError::RepositoryError; };
}
