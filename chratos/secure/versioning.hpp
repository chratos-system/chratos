#pragma once

#include <chratos/lib/blocks.hpp>
#include <chratos/node/lmdb.hpp>
#include <chratos/secure/utility.hpp>

namespace chratos
{
class account_info_v1
{
public:
	account_info_v1 ();
	account_info_v1 (MDB_val const &);
	account_info_v1 (chratos::account_info_v1 const &) = default;
	account_info_v1 (chratos::block_hash const &, chratos::block_hash const &, chratos::amount const &, uint64_t);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	chratos::mdb_val val () const;
	chratos::block_hash head;
	chratos::block_hash rep_block;
	chratos::amount balance;
	uint64_t modified;
};
class pending_info_v3
{
public:
	pending_info_v3 ();
	pending_info_v3 (MDB_val const &);
	pending_info_v3 (chratos::account const &, chratos::amount const &, chratos::account const &);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	bool operator== (chratos::pending_info_v3 const &) const;
	chratos::mdb_val val () const;
	chratos::account source;
	chratos::amount amount;
	chratos::account destination;
};
// Latest information about an account
class account_info_v5
{
public:
	account_info_v5 ();
	account_info_v5 (MDB_val const &);
	account_info_v5 (chratos::account_info_v5 const &) = default;
	account_info_v5 (chratos::block_hash const &, chratos::block_hash const &, chratos::block_hash const &, chratos::amount const &, uint64_t);
	void serialize (chratos::stream &) const;
	bool deserialize (chratos::stream &);
	chratos::mdb_val val () const;
	chratos::block_hash head;
	chratos::block_hash rep_block;
	chratos::block_hash open_block;
	chratos::amount balance;
	uint64_t modified;
};
}
