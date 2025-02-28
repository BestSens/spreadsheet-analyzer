#pragma once

#include <random>
#include <string>
#include <utility>

#include "uuid.h"

class UUIDGenerator {
public:
	// Returns the singleton instance.
	static auto getInstance() -> UUIDGenerator & {
		static UUIDGenerator instance;
		return instance;
	}

	// Delete copy constructor, move constructor, copy assignment and move assignment operators.
	UUIDGenerator(const UUIDGenerator &) = delete;
	UUIDGenerator(UUIDGenerator &&) = delete;
	auto operator=(const UUIDGenerator &) -> UUIDGenerator & = delete;
	auto operator=(UUIDGenerator &&) -> UUIDGenerator & = delete;

	// Generates a new UUID.
	auto generate() -> uuids::uuid {
		return this->generator();
	}

private:
	// Private constructor initializes the generator.
	UUIDGenerator() = default;
	~UUIDGenerator() = default;

	std::mt19937 rng{std::random_device{}()};
	uuids::uuid_random_generator generator{rng};
};