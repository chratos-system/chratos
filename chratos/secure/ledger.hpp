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
	chratos::account account (chratos::transaction const &, chratos::block_hash const &);
	chratos::uint128_t amount (chratos::transaction const &, chratos::block_hash const &);
	chratos::uint128_t balance (chratos::transaction const &, chratos::block_hash const &);
	chratos::uint128_t account_balance (chratos::transaction const &, chratos::account const &);
	chratos::uint128_t account_pending (chratos::transaction const &, chratos::account const &);
	chratos::uint128_t weight (chratos::transaction const &, chratos::account const &);
	std::unique_ptr<chratos::block> successor (chratos::transaction const &, chratos::block_hash const &);
	std::unique_ptr<chratos::block> forked_block (chratos::transaction const &, chratos::block const &);
	chratos::block_hash latest (chratos::transaction const &, chratos::account const &);
	chratos::block_hash latest_root (chratos::transaction const &, chratos::account const &);
	chratos::block_hash latest_dividend (chratos::transaction const &);
	chratos::block_hash representative (chratos::transaction const &, chratos::block_hash const &);
	chratos::block_hash representative_calculated (chratos::transaction const &, chratos::block_hash const &);
	bool block_exists (chratos::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (chratos::block_hash const &);
	bool is_send (chratos::transaction const &, chratos::state_block const &);
	bool is_dividend (chratos::transaction const &, chratos::state_block const &);
	bool is_dividend_claim (chratos::transaction const &, chratos::state_block const &);
	bool has_outstanding_pendings_for_dividend (chratos::transaction const &, chratos::block_hash const &, chratos::account const &);
	bool dividends_are_ordered (chratos::transaction const &, chratos::block_hash const &, chratos::block_hash const &);
	chratos::amount amount_for_dividend (chratos::transaction const &, chratos::block_hash const &, chratos::account const &);
	std::vector<chratos::block_hash> unclaimed_for_account (chratos::transaction const &, chratos::account const &);
	chratos::amount burn_account_balance (chratos::transaction const &, chratos::block_hash const &);
	std::vector<std::shared_ptr<chratos::block>> dividend_claim_blocks (chratos::transaction const &, chratos::account const &);
	std::unordered_map<chratos::block_hash, int> get_dividend_indexes (chratos::transaction const &);
	chratos::block_hash block_destination (chratos::transaction const &, chratos::block const &);
	chratos::block_hash block_source (chratos::transaction const &, chratos::block const &);
	chratos::process_return process (chratos::transaction const &, chratos::block const &, bool = false);
	void rollback (chratos::transaction const &, chratos::block_hash const &);
	void change_latest (chratos::transaction const &, chratos::account const &, chratos::block_hash const &, chratos::account const &, chratos::block_hash const &, chratos::uint128_union const &, uint64_t, bool = false, chratos::epoch = chratos::epoch::epoch_0);
	void checksum_update (chratos::transaction const &, chratos::block_hash const &);
	chratos::checksum checksum (chratos::transaction const &, chratos::account const &, chratos::account const &);
	void dump_account_chain (chratos::account const &);
	bool could_fit (chratos::transaction const &, chratos::block const &);
	bool is_epoch_link (chratos::uint256_union const &);
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
