#pragma once

#include <chrono>
#include <cstddef>

namespace chratos
{
/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used for comparison; do not change.
 */
// Network variants with different genesis blocks and network parameters
enum class chratos_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	chratos_test_network = 0,
	// Normal work parameters, secret beta genesis key, beta IP ports
	chratos_beta_network = 1,
	// Normal work parameters, secret live key, live IP ports
	chratos_live_network = 2
};
chratos::chratos_networks const chratos_network = chratos_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
