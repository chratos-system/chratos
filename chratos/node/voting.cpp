#include <chratos/node/voting.hpp>

#include <chratos/node/node.hpp>

chratos::vote_generator::vote_generator (chratos::node & node_a, std::chrono::milliseconds wait_a) :
node (node_a),
wait (wait_a),
stopped (false),
started (false),
thread ([this]() { run (); })
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void chratos::vote_generator::add (chratos::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	hashes.push_back (hash_a);
	condition.notify_all ();
}

void chratos::vote_generator::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	lock.unlock ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void chratos::vote_generator::send (std::unique_lock<std::mutex> & lock_a)
{
	std::vector<chratos::block_hash> hashes_l;
	hashes_l.reserve (12);
	while (!hashes.empty () && hashes_l.size () < 12)
	{
		hashes_l.push_back (hashes.front ());
		hashes.pop_front ();
	}
	lock_a.unlock ();
	{
		auto transaction (node.store.tx_begin_read ());
		node.wallets.foreach_representative (transaction, [this, &hashes_l, &transaction](chratos::public_key const & pub_a, chratos::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction, pub_a, prv_a, hashes_l));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
		});
	}
	lock_a.lock ();
}

void chratos::vote_generator::run ()
{
	chratos::thread_role::set (chratos::thread_role::name::voting);
	std::unique_lock<std::mutex> lock (mutex);
	started = true;
	condition.notify_all ();
	auto min (std::numeric_limits<std::chrono::steady_clock::time_point>::min ());
	auto cutoff (min);
	while (!stopped)
	{
		auto now (std::chrono::steady_clock::now ());
		if (hashes.size () >= 12)
		{
			send (lock);
		}
		else if (cutoff == min) // && hashes.size () < 12
		{
			cutoff = now + wait;
			condition.wait_until (lock, cutoff);
		}
		else if (now < cutoff) // && hashes.size () < 12
		{
			condition.wait_until (lock, cutoff);
		}
		else // now >= cutoff && hashes.size () < 12
		{
			cutoff = min;
			if (!hashes.empty ())
			{
				send (lock);
			}
			else
			{
				condition.wait (lock);
			}
		}
	}
}
