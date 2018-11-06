#pragma once

#include <boost/asio/ip/address_v4.hpp>
#include <chratos/lib/config.hpp>
#include <miniupnpc.h>
#include <mutex>

namespace chratos
{
class node;

/** Collected protocol information */
class mapping_protocol
{
public:
	/** Protocol name; TPC or UDP */
	char const * name;
	int remaining;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
};

/** UPnP port mapping */
class port_mapping
{
public:
	port_mapping (chratos::node &);
	void start ();
	void stop ();
	void refresh_devices ();

private:
	/** Add port mappings for the node port (not RPC). Refresh when the lease ends. */
	void refresh_mapping ();
	/** Refresh occasionally in case router loses mapping */
	void check_mapping_loop ();
	int check_mapping ();
	std::mutex mutex;
	chratos::node & node;
	/** List of all UPnP devices */
	UPNPDev * devices;
	/** UPnP collected url information */
	UPNPUrls urls;
	/** UPnP state */
	IGDdatas data;
	/** Timeouts are primes so they infrequently happen at the same time */
	static int constexpr mapping_timeout = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? 53 : 3593;
	static int constexpr check_timeout = chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? 17 : 53;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count;
	bool on;
};
}
