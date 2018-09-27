#pragma once

#include <chratos/secure/common.hpp>

struct MDB_txn;
namespace chratos
{
class block_store;
class stat;

class shared_ptr_block_hash
{
public:
	size_t operator() (std::shared_ptr<chratos::block> const &) const;
	bool operator() (std::shared_ptr<chratos::block> const &, std::shared_ptr<chratos::block> const &) const;
};
using tally_t = std::map<chratos::uint128_t, std::shared_ptr<chratos::block>, std::greater<chratos::uint128_t>>;
class ledger
{
public:
	ledger (chratos::block_store &, chratos::stat &, chratos::uint256_union const & = 1, chratos::account const & = 0);
	chratos::account account (MDB_txn *, chratos::block_hash const &);
	chratos::uint128_t amount (MDB_txn *, chratos::block_hash const &);
	chratos::uint128_t balance (MDB_txn *, chratos::block_hash const &);
	chratos::uint128_t account_balance (MDB_txn *, chratos::account const &);
	chratos::uint128_t account_pending (MDB_txn *, chratos::account const &);
	chratos::uint128_t weight (MDB_txn *, chratos::account const &);
	std::unique_ptr<chratos::block> successor (MDB_txn *, chratos::block_hash const &);
	std::unique_ptr<chratos::block> forked_block (MDB_txn *, chratos::block const &);
	chratos::block_hash latest (MDB_txn *, chratos::account const &);
	chratos::block_hash latest_root (MDB_txn *, chratos::account const &);
  chratos::block_hash latest_dividend (MDB_txn *);
	chratos::block_hash representative (MDB_txn *, chratos::block_hash const &);
	chratos::block_hash representative_calculated (MDB_txn *, chratos::block_hash const &);
	bool block_exists (chratos::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (chratos::block_hash const &);
	bool is_send (MDB_txn *, chratos::state_block const &);
	bool is_dividend (MDB_txn *, chratos::state_block const &);
	bool is_dividend_claim (MDB_txn *, chratos::state_block const &);
  bool has_outstanding_pendings_for_dividend (MDB_txn *, chratos::block_hash const &, chratos::account const &);
  bool dividends_are_ordered (MDB_txn *, chratos::block_hash const &, chratos::block_hash const &);
  chratos::amount amount_for_dividend (MDB_txn *, chratos::block_hash const &, chratos::account const &);
  std::vector<std::shared_ptr<chratos::block>> dividend_claim_blocks (MDB_txn *, chratos::account const &);
  std::unordered_map<chratos::block_hash, int> get_dividend_indexes (MDB_txn *);
	chratos::block_hash block_destination (MDB_txn *, chratos::block const &);
	chratos::block_hash block_source (MDB_txn *, chratos::block const &);
	chratos::process_return process (MDB_txn *, chratos::block const &);
	void rollback (MDB_txn *, chratos::block_hash const &);
	void change_latest (MDB_txn *, chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::block_hash const &, chratos::uint128_union const &, uint64_t, bool = false, chratos::epoch = chratos::epoch::epoch_0);
	void checksum_update (MDB_txn *, chratos::block_hash const &);
	chratos::checksum checksum (MDB_txn *, chratos::account const &, chratos::account const &);
	void dump_account_chain (chratos::account const &);
	bool could_fit (MDB_txn *, chratos::block const &);
	static chratos::uint128_t const unit;
	chratos::block_store & store;
	chratos::stat & stats;
	std::unordered_map<chratos::account, chratos::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
	chratos::uint256_union epoch_link;
	chratos::account epoch_signer;
};
};
