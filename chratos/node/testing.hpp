#pragma once

#include <chratos/lib/errors.hpp>
#include <chratos/node/node.hpp>
#include <chrono>

namespace chratos
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system
{
public:
	system (uint16_t, size_t);
	~system ();
	void generate_activity (chratos::node &, std::vector<chratos::account> &);
	void generate_mass_activity (uint32_t, chratos::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	chratos::account get_random_account (std::vector<chratos::account> &);
	chratos::uint128_t get_random_amount (chratos::transaction const &, chratos::node &, chratos::account const &);
	void generate_rollback (chratos::node &, std::vector<chratos::account> &);
	void generate_change_known (chratos::node &, std::vector<chratos::account> &);
	void generate_change_unknown (chratos::node &, std::vector<chratos::account> &);
	void generate_receive (chratos::node &);
	void generate_send_new (chratos::node &, std::vector<chratos::account> &);
	void generate_send_existing (chratos::node &, std::vector<chratos::account> &);
	std::shared_ptr<chratos::wallet> wallet (size_t);
	chratos::account account (chratos::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or chratos::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	boost::asio::io_service service;
	chratos::alarm alarm;
	std::vector<std::shared_ptr<chratos::node>> nodes;
	chratos::logging logging;
	chratos::work_pool work;
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline { std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor { 1.0 };
};
class landing_store
{
public:
	landing_store ();
	landing_store (chratos::account const &, chratos::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	chratos::account source;
	chratos::account destination;
	uint64_t start;
	uint64_t last;
	bool deserialize (std::istream &);
	void serialize (std::ostream &) const;
	bool operator== (chratos::landing_store const &) const;
};
class landing
{
public:
	landing (chratos::node &, std::shared_ptr<chratos::wallet>, chratos::landing_store &, boost::filesystem::path const &);
	void write_store ();
	chratos::uint128_t distribution_amount (uint64_t);
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	chratos::landing_store & store;
	std::shared_ptr<chratos::wallet> wallet;
	chratos::node & node;
	static int constexpr interval_exponent = 10;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
REGISTER_ERROR_CODES (chratos, error_system);
