#include <chratos/node/common.hpp>
#include <chratos/node/wallet.hpp>
#include <chratos/secure/blockstore.hpp>
#include <queue>

#include <boost/polymorphic_cast.hpp>

chratos::amount_visitor::amount_visitor (chratos::transaction const & transaction_a, chratos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_amount (0),
current_balance (0),
amount (0)
{
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

chratos::balance_visitor::balance_visitor (chratos::transaction const & transaction_a, chratos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_balance (0),
current_amount (0),
balance (0)
{
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

chratos::representative_visitor::representative_visitor (chratos::transaction const & transaction_a, chratos::block_store & store_a) :
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
