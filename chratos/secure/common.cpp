#include <chratos/secure/common.hpp>

#include <chratos/lib/interface.h>
#include <chratos/node/common.hpp>
#include <chratos/secure/blockstore.hpp>
#include <chratos/secure/versioning.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <ed25519-donna/ed25519.h>

// Genesis keys for network variants
namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // chr_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "5DB43C7501AC8C1CE5C21C9CF4F2EA1973205F315BF419BD3401B2D3A009740D"; // chr_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp
char const * live_public_key_data = "7E5EB032362A11DC9A591E53A12F9E231BE8FD5B25F1BAA4BAA44508DCAA0181"; // chr_1zkyp1s5ecijukf7k9kmn6qswarux5yopbhjqckdob4735gcn1e34rpi75p4
char const * test_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "chr_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "chr_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "9680625b39d3363d",
  "dividend": "0000000000000000000000000000000000000000000000000000000000000000",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
})%%%";

char const * beta_genesis_data = R"%%%({
  "type": "open",
  "source": "5DB43C7501AC8C1CE5C21C9CF4F2EA1973205F315BF419BD3401B2D3A009740D",
  "representative": "chr_1qfn9jti5d6e5mkw696wymsgn8dm63hm4pzn58yma1fktgi1kx1f9c5b35gb",
  "account": "chr_1qfn9jti5d6e5mkw696wymsgn8dm63hm4pzn58yma1fktgi1kx1f9c5b35gb",
  "work": "4a18a369468685b2",
  "dividend": "0000000000000000000000000000000000000000000000000000000000000000",
  "signature": "BBCF0BC4873D0007F338A980BC9BEDB1481B19507244E063DBB488BDB2977929F83E1300202DC6D997D8FDC2AA055D7123345698F580BF9A44104D0EAD8CDC0A"
})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "7E5EB032362A11DC9A591E53A12F9E231BE8FD5B25F1BAA4BAA44508DCAA0181",
	"representative": "chr_1zkyp1s5ecijukf7k9kmn6qswarux5yopbhjqckdob4735gcn1e34rpi75p4",
	"account": "chr_1zkyp1s5ecijukf7k9kmn6qswarux5yopbhjqckdob4735gcn1e34rpi75p4",
	"work": "ace2c7809d970ebd",
  "dividend": "0000000000000000000000000000000000000000000000000000000000000000",
	"signature": "124D3D5BD0A6062587876C475BE0D27D69C8B6534B3E9905222A71245F2DEEFDAA150AA3206A14EBF62D7AFBD04BE84D594B3B5641107C94C460B251288A4001"
})%%%";

class ledger_constants
{
public:
	ledger_constants () :
	zero_key ("0"),
	test_genesis_key (test_private_key_data),
	chratos_test_account (test_public_key_data),
	chratos_beta_account (beta_public_key_data),
	chratos_live_account (live_public_key_data),
	chratos_test_genesis (test_genesis_data),
	chratos_beta_genesis (beta_genesis_data),
	chratos_live_genesis (live_genesis_data),
	genesis_account (chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? chratos_test_account : chratos::chratos_network == chratos::chratos_networks::chratos_beta_network ? chratos_beta_account : chratos_live_account),
	genesis_block (chratos::chratos_network == chratos::chratos_networks::chratos_test_network ? chratos_test_genesis : chratos::chratos_network == chratos::chratos_networks::chratos_beta_network ? chratos_beta_genesis : chratos_live_genesis),
	genesis_amount (std::numeric_limits<chratos::uint128_t>::max ()),
	burn_account (0),
  dividend_base (0)
	{
		CryptoPP::AutoSeededRandomPool random_pool;
		// Randomly generating these mean no two nodes will ever have the same sentinel values which protects against some insecure algorithms
		random_pool.GenerateBlock (not_a_block.bytes.data (), not_a_block.bytes.size ());
		random_pool.GenerateBlock (not_an_account.bytes.data (), not_an_account.bytes.size ());
	}
	chratos::keypair zero_key;
	chratos::keypair test_genesis_key;
	chratos::account chratos_test_account;
	chratos::account chratos_beta_account;
	chratos::account chratos_live_account;
	std::string chratos_test_genesis;
	std::string chratos_beta_genesis;
	std::string chratos_live_genesis;
	chratos::account genesis_account;
	std::string genesis_block;
	chratos::uint128_t genesis_amount;
	chratos::block_hash not_a_block;
	chratos::account not_an_account;
	chratos::account burn_account;
  chratos::block_hash dividend_base;
};
ledger_constants globals;
}

size_t constexpr chratos::send_block::size;
size_t constexpr chratos::receive_block::size;
size_t constexpr chratos::open_block::size;
size_t constexpr chratos::change_block::size;
size_t constexpr chratos::state_block::size;

chratos::keypair const & chratos::zero_key (globals.zero_key);
chratos::keypair const & chratos::test_genesis_key (globals.test_genesis_key);
chratos::account const & chratos::chratos_test_account (globals.chratos_test_account);
chratos::account const & chratos::chratos_beta_account (globals.chratos_beta_account);
chratos::account const & chratos::chratos_live_account (globals.chratos_live_account);
std::string const & chratos::chratos_test_genesis (globals.chratos_test_genesis);
std::string const & chratos::chratos_beta_genesis (globals.chratos_beta_genesis);
std::string const & chratos::chratos_live_genesis (globals.chratos_live_genesis);

chratos::account const & chratos::genesis_account (globals.genesis_account);
std::string const & chratos::genesis_block (globals.genesis_block);
chratos::uint128_t const & chratos::genesis_amount (globals.genesis_amount);
chratos::block_hash const & chratos::not_a_block (globals.not_a_block);
chratos::block_hash const & chratos::not_an_account (globals.not_an_account);
chratos::account const & chratos::burn_account (globals.burn_account);
chratos::block_hash const & chratos::dividend_base (globals.dividend_base);

// Create a new random keypair
chratos::keypair::keypair ()
{
	random_pool.GenerateBlock (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
chratos::keypair::keypair (chratos::raw_key && prv_a) :
prv (std::move (prv_a))
{
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
chratos::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void chratos::serialize_block (chratos::stream & stream_a, chratos::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

chratos::account_info::account_info () :
head (0),
rep_block (0),
open_block (0),
dividend_block(dividend_base),
balance (0),
modified (0),
block_count (0),
epoch (chratos::epoch::epoch_0)
{
}

chratos::account_info::account_info (chratos::block_hash const & head_a, chratos::block_hash const & rep_block_a, chratos::block_hash const & open_block_a, chratos::block_hash const & dividend_block_a, chratos::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, chratos::epoch epoch_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
dividend_block (dividend_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch (epoch_a)
{
}

void chratos::account_info::serialize (chratos::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, open_block.bytes);
  write (stream_a, dividend_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
	write (stream_a, block_count);
}

bool chratos::account_info::deserialize (chratos::stream & stream_a)
{
	auto error (read (stream_a, head.bytes));
	if (!error)
	{
		error = read (stream_a, rep_block.bytes);
		if (!error)
		{
			error = read (stream_a, open_block.bytes);
			if (!error)
			{
        error = read (stream_a, dividend_block.bytes);
        if (!error)
        {
          error = read (stream_a, balance.bytes);
          if (!error)
          {
            error = read (stream_a, modified);
            if (!error)
            {
              error = read (stream_a, block_count);
            }
          }
        }
			}
		}
	}
	return error;
}

bool chratos::account_info::operator== (chratos::account_info const & other_a) const
{
	return head == other_a.head && rep_block == other_a.rep_block && open_block == other_a.open_block && dividend_block == other_a.dividend_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch == other_a.epoch;
}

bool chratos::account_info::operator!= (chratos::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t chratos::account_info::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&rep_block));
	assert (reinterpret_cast<const uint8_t *> (&rep_block) + sizeof (rep_block) == reinterpret_cast<const uint8_t *> (&open_block));
	assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&dividend_block));
	assert (reinterpret_cast<const uint8_t *> (&dividend_block) + sizeof (dividend_block) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	return sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (dividend_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count);
}

chratos::dividend_info::dividend_info () :
head (dividend_base),
balance (0),
modified (0),
block_count (0),
epoch (chratos::epoch::epoch_1)
{
}

chratos::dividend_info::dividend_info (chratos::block_hash const & head_a, chratos::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, chratos::epoch epoch_a) :
head (head_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch (epoch_a)
{
}

void chratos::dividend_info::serialize (chratos::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
	write (stream_a, block_count);
}

bool chratos::dividend_info::deserialize (chratos::stream & stream_a)
{
  auto error (read (stream_a, head.bytes));
	if (!error)
	{
    error = read (stream_a, balance.bytes);
    if (!error)
    {
      error = read (stream_a, modified);
      if (!error)
      {
        error = read (stream_a, block_count);
      }
    }
	}
	return error;
}

bool chratos::dividend_info::operator== (chratos::dividend_info const & other_a) const
{

	return head == other_a.head && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch == other_a.epoch;
}

bool chratos::dividend_info::operator!= (chratos::dividend_info const & other_a) const
{
	return !(*this == other_a);
}

size_t chratos::dividend_info::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	return sizeof (head) + sizeof (balance) + sizeof (modified) + sizeof (block_count);
}

chratos::block_counts::block_counts () :
send (0),
receive (0),
open (0),
change (0),
state_v0 (0),
state_v1 (0),
dividend (0),
claim (0)
{
}

size_t chratos::block_counts::sum ()
{
	return send + receive + open + change + state_v0 + state_v1 + dividend + claim;
}

chratos::pending_info::pending_info () :
source (0),
amount (0),
dividend (0),
epoch (chratos::epoch::epoch_0)
{
}

chratos::pending_info::pending_info (chratos::account const & source_a, chratos::amount const & amount_a, chratos::block_hash const & dividend_a, chratos::epoch epoch_a) :
source (source_a),
amount (amount_a),
dividend (dividend_a),
epoch (epoch_a)
{
}

void chratos::pending_info::serialize (chratos::stream & stream_a) const
{
	chratos::write (stream_a, source.bytes);
	chratos::write (stream_a, amount.bytes);
	chratos::write (stream_a, dividend.bytes);
}

bool chratos::pending_info::deserialize (chratos::stream & stream_a)
{
	auto result (chratos::read (stream_a, source.bytes));
	if (!result)
	{
		result = chratos::read (stream_a, amount.bytes);
    if (!result)
    {
      result = chratos::read (stream_a, dividend.bytes);
    }
	}
	return result;
}

bool chratos::pending_info::operator== (chratos::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && dividend == other_a.dividend && epoch == other_a.epoch;
}

chratos::pending_key::pending_key () :
account (0),
hash (0)
{
}

chratos::pending_key::pending_key (chratos::account const & account_a, chratos::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

void chratos::pending_key::serialize (chratos::stream & stream_a) const
{
	chratos::write (stream_a, account.bytes);
	chratos::write (stream_a, hash.bytes);
}

bool chratos::pending_key::deserialize (chratos::stream & stream_a)
{
	auto error (chratos::read (stream_a, account.bytes));
	if (!error)
	{
		error = chratos::read (stream_a, hash.bytes);
	}
	return error;
}

bool chratos::pending_key::operator== (chratos::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

chratos::block_info::block_info () :
account (0),
balance (0)
{
}

chratos::block_info::block_info (chratos::account const & account_a, chratos::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

void chratos::block_info::serialize (chratos::stream & stream_a) const
{
	chratos::write (stream_a, account.bytes);
	chratos::write (stream_a, balance.bytes);
}

bool chratos::block_info::deserialize (chratos::stream & stream_a)
{
	auto error (chratos::read (stream_a, account.bytes));
	if (!error)
	{
		error = chratos::read (stream_a, balance.bytes);
	}
	return error;
}

bool chratos::block_info::operator== (chratos::block_info const & other_a) const
{
	return account == other_a.account && balance == other_a.balance;
}

bool chratos::vote::operator== (chratos::vote const & other_a) const
{
	auto blocks_equal (true);
	if (blocks.size () != other_a.blocks.size ())
	{
		blocks_equal = false;
	}
	else
	{
		for (auto i (0); blocks_equal && i < blocks.size (); ++i)
		{
			auto block (blocks[i]);
			auto other_block (other_a.blocks[i]);
			if (block.which () != other_block.which ())
			{
				blocks_equal = false;
			}
			else if (block.which ())
			{
				if (boost::get<chratos::block_hash> (block) != boost::get<chratos::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<chratos::block>> (block) == *boost::get<std::shared_ptr<chratos::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return sequence == other_a.sequence && blocks_equal && account == other_a.account && signature == other_a.signature;
}

bool chratos::vote::operator!= (chratos::vote const & other_a) const
{
	return !(*this == other_a);
}

std::string chratos::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (sequence));
	boost::property_tree::ptree blocks_tree;
	for (auto block : blocks)
	{
		if (block.which ())
		{
			blocks_tree.put ("", boost::get<std::shared_ptr<chratos::block>> (block)->to_json ());
		}
		else
		{
			blocks_tree.put ("", boost::get<std::shared_ptr<chratos::block>> (block)->hash ().to_string ());
		}
	}
	tree.add_child ("blocks", blocks_tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

chratos::amount_visitor::amount_visitor (MDB_txn * transaction_a, chratos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_amount (0),
current_balance (0),
amount (0)
{
}

void chratos::amount_visitor::send_block (chratos::send_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void chratos::amount_visitor::receive_block (chratos::receive_block const & block_a)
{
	current_amount = block_a.hashables.source;
}

void chratos::amount_visitor::open_block (chratos::open_block const & block_a)
{
	if (block_a.hashables.source != chratos::genesis_account)
	{
		current_amount = block_a.hashables.source;
	}
	else
	{
		amount = chratos::genesis_amount;
		current_amount = 0;
	}
}

void chratos::amount_visitor::state_block (chratos::state_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void chratos::amount_visitor::dividend_block (chratos::dividend_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void chratos::amount_visitor::claim_block (chratos::claim_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void chratos::amount_visitor::change_block (chratos::change_block const & block_a)
{
	amount = 0;
	current_amount = 0;
}

void chratos::amount_visitor::compute (chratos::block_hash const & block_hash)
{
	current_amount = block_hash;
	while (!current_amount.is_zero () || !current_balance.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			auto block (store.block_get (transaction, current_amount));
			if (block != nullptr)
			{
				block->visit (*this);
			}
			else
			{
				if (block_hash == chratos::genesis_account)
				{
					amount = std::numeric_limits<chratos::uint128_t>::max ();
					current_amount = 0;
				}
				else
				{
					assert (false);
					amount = 0;
					current_amount = 0;
				}
			}
		}
		else
		{
			balance_visitor prev (transaction, store);
			prev.compute (current_balance);
			amount = amount < prev.balance ? prev.balance - amount : amount - prev.balance;
			current_balance = 0;
		}
	}
}

chratos::balance_visitor::balance_visitor (MDB_txn * transaction_a, chratos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_balance (0),
current_amount (0),
balance (0)
{
}

void chratos::balance_visitor::send_block (chratos::send_block const & block_a)
{
	balance += block_a.hashables.balance.number ();
	current_balance = 0;
}

void chratos::balance_visitor::receive_block (chratos::receive_block const & block_a)
{
	chratos::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_amount = block_a.hashables.source;
		current_balance = block_a.hashables.previous;
	}
}

void chratos::balance_visitor::open_block (chratos::open_block const & block_a)
{
	current_amount = block_a.hashables.source;
	current_balance = 0;
}

void chratos::balance_visitor::change_block (chratos::change_block const & block_a)
{
	chratos::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_balance = block_a.hashables.previous;
	}
}

void chratos::balance_visitor::state_block (chratos::state_block const & block_a)
{
	balance = block_a.hashables.balance.number ();
	current_balance = 0;
}

void chratos::balance_visitor::dividend_block (chratos::dividend_block const & block_a)
{
	balance += block_a.hashables.balance.number ();
	current_balance = 0;
}

void chratos::balance_visitor::claim_block (chratos::claim_block const & block_a)
{
	balance += block_a.hashables.balance.number ();
  current_balance = 0;
}

void chratos::balance_visitor::compute (chratos::block_hash const & block_hash)
{
	current_balance = block_hash;
	while (!current_balance.is_zero () || !current_amount.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			amount_visitor source (transaction, store);
			source.compute (current_amount);
			balance += source.amount;
			current_amount = 0;
		}
		else
		{
			auto block (store.block_get (transaction, current_balance));
			assert (block != nullptr);
			block->visit (*this);
		}
	}
}

chratos::representative_visitor::representative_visitor (MDB_txn * transaction_a, chratos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
result (0)
{
}

void chratos::representative_visitor::compute (chratos::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		assert (block != nullptr);
		block->visit (*this);
	}
}

void chratos::representative_visitor::send_block (chratos::send_block const & block_a)
{
	current = block_a.previous ();
}

void chratos::representative_visitor::receive_block (chratos::receive_block const & block_a)
{
	current = block_a.previous ();
}

void chratos::representative_visitor::open_block (chratos::open_block const & block_a)
{
	result = block_a.hash ();
}

void chratos::representative_visitor::change_block (chratos::change_block const & block_a)
{
	result = block_a.hash ();
}

void chratos::representative_visitor::state_block (chratos::state_block const & block_a)
{
	result = block_a.hash ();
}

void chratos::representative_visitor::dividend_block (chratos::dividend_block const & block_a)
{
	current = block_a.hash ();
}

void chratos::representative_visitor::claim_block (chratos::claim_block const & block_a)
{
	current = block_a.hash ();
}

chratos::vote::vote (chratos::vote const & other_a) :
sequence (other_a.sequence),
blocks (other_a.blocks),
account (other_a.account),
signature (other_a.signature)
{
}

chratos::vote::vote (bool & error_a, chratos::stream & stream_a)
{
	error_a = deserialize (stream_a);
}

chratos::vote::vote (bool & error_a, chratos::stream & stream_a, chratos::block_type type_a)
{
	if (!error_a)
	{
		error_a = chratos::read (stream_a, account.bytes);
		if (!error_a)
		{
			error_a = chratos::read (stream_a, signature.bytes);
			if (!error_a)
			{
				error_a = chratos::read (stream_a, sequence);
				if (!error_a)
				{
					while (!error_a && stream_a.in_avail () > 0)
					{
						if (type_a == chratos::block_type::not_a_block)
						{
							chratos::block_hash block_hash;
							error_a = chratos::read (stream_a, block_hash);
							if (!error_a)
							{
								blocks.push_back (block_hash);
							}
						}
						else
						{
							std::shared_ptr<chratos::block> block (chratos::deserialize_block (stream_a, type_a));
							error_a = block == nullptr;
							if (!error_a)
							{
								blocks.push_back (block);
							}
						}
					}
					if (blocks.empty ())
					{
						error_a = true;
					}
				}
			}
		}
	}
}

chratos::vote::vote (chratos::account const & account_a, chratos::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<chratos::block> block_a) :
sequence (sequence_a),
blocks (1, block_a),
account (account_a),
signature (chratos::sign_message (prv_a, account_a, hash ()))
{
}

chratos::vote::vote (chratos::account const & account_a, chratos::raw_key const & prv_a, uint64_t sequence_a, std::vector<chratos::block_hash> blocks_a) :
sequence (sequence_a),
account (account_a)
{
	assert (blocks_a.size () > 0);
	for (auto hash : blocks_a)
	{
		blocks.push_back (hash);
	}
	signature = chratos::sign_message (prv_a, account_a, hash ());
}

std::string chratos::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string chratos::vote::hash_prefix = "vote ";

chratos::uint256_union chratos::vote::hash () const
{
	chratos::uint256_union result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	if (blocks.size () > 1 || (blocks.size () > 0 && blocks[0].which ()))
	{
		blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	}
	for (auto block_hash : *this)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = sequence;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void chratos::vote::serialize (chratos::stream & stream_a, chratos::block_type type)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			assert (type == chratos::block_type::not_a_block);
			write (stream_a, boost::get<chratos::block_hash> (block));
		}
		else
		{
			if (type == chratos::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<chratos::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<chratos::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void chratos::vote::serialize (chratos::stream & stream_a)
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, chratos::block_type::not_a_block);
			write (stream_a, boost::get<chratos::block_hash> (block));
		}
		else
		{
			chratos::serialize_block (stream_a, *boost::get<std::shared_ptr<chratos::block>> (block));
		}
	}
}

bool chratos::vote::deserialize (chratos::stream & stream_a)
{
	auto result (read (stream_a, account));
	if (!result)
	{
		result = read (stream_a, signature);
		if (!result)
		{
			result = read (stream_a, sequence);
			if (!result)
			{
				chratos::block_type type;
				while (!result)
				{
					if (chratos::read (stream_a, type))
					{
						if (blocks.empty ())
						{
							result = true;
						}
						break;
					}
					if (!result)
					{
						if (type == chratos::block_type::not_a_block)
						{
							chratos::block_hash block_hash;
							result = chratos::read (stream_a, block_hash);
							if (!result)
							{
								blocks.push_back (block_hash);
							}
						}
						else
						{
							std::shared_ptr<chratos::block> block (chratos::deserialize_block (stream_a, type));
							result = block == nullptr;
							if (!result)
							{
								blocks.push_back (block);
							}
						}
					}
				}
			}
		}
	}
	return result;
}

bool chratos::vote::validate ()
{
	auto result (chratos::validate_message (account, hash (), signature));
	return result;
}

chratos::block_hash chratos::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<chratos::block>, chratos::block_hash> const & item) const
{
	chratos::block_hash result;
	if (item.which ())
	{
		result = boost::get<chratos::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<chratos::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> chratos::vote::begin () const
{
	return boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> (blocks.begin (), chratos::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> chratos::vote::end () const
{
	return boost::transform_iterator<chratos::iterate_vote_blocks_as_hash, chratos::vote_blocks_vec_iter> (blocks.end (), chratos::iterate_vote_blocks_as_hash ());
}

chratos::genesis::genesis ()
{
	boost::property_tree::ptree tree;
	std::stringstream istream (chratos::genesis_block);
	boost::property_tree::read_json (istream, tree);
	auto block (chratos::deserialize_block_json (tree));
	assert (dynamic_cast<chratos::open_block *> (block.get ()) != nullptr);
	open.reset (static_cast<chratos::open_block *> (block.release ()));
}

chratos::block_hash chratos::genesis::hash () const
{
	return open->hash ();
}
