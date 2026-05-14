#pragma once
#include <random>
#include <string>

// Doesn't need to be full 128 bit randomness I don't think
// https://stackoverflow.com/questions/24365331/how-can-i-generate-uuid-in-c-without-using-boost-library
namespace Tools {

namespace UUID {
static std::random_device rd;
static std::mt19937_64 gen{rd()};
static std::uniform_int_distribution<> dist(0, 15);
static std::uniform_int_distribution<> dist2(8, 11);
}; // namespace UUID

std::string uuid();
}; // namespace Tools
