#pragma once

#include <chratos/lib/numbers.hpp>

#include <boost/thread.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace chratos
{
class node;
class vote_generator
{
public:
	vote_generator (chratos::node &, std::chrono::milliseconds);
	void add (chratos::block_hash const &);
	void stop ();

private:
	void run ();
	void send (std::unique_lock<std::mutex> &);
	chratos::node & node;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<chratos::block_hash> hashes;
	std::chrono::milliseconds wait;
	bool stopped;
	bool started;
	boost::thread thread;
};
}
