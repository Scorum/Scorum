#include <scorum/chain/genesis/initializators/steemit_bounty_account_initializator.hpp>
#include <scorum/chain/data_service_factory.hpp>

#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/account.hpp>

#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/dynamic_global_property_object.hpp>

#include <scorum/chain/genesis/genesis_state.hpp>

#include <fc/exception/exception.hpp>

namespace scorum {
namespace chain {
namespace genesis {

void steemit_bounty_account_initializator_impl::apply(data_service_factory_i& services,
                                                      const genesis_state_type& genesis_state)
{
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();
    account_service_i& account_service = services.account_service();

    asset sp_accounts_supply = genesis_state.steemit_bounty_accounts_supply;

    FC_ASSERT(sp_accounts_supply.symbol() == VESTS_SYMBOL);

    for (auto& account : genesis_state.steemit_bounty_accounts)
    {
        FC_ASSERT(!account.name.empty(), "Account 'name' should not be empty.");

        account_service.check_account_existence(account.name);

        FC_ASSERT(account.sp_amount.symbol() == VESTS_SYMBOL, "Invalid asset symbol for '${1}'.", ("1", account.name));

        FC_ASSERT(account.sp_amount.amount > (share_value_type)0, "Invalid asset amount for '${1}'.",
                  ("1", account.name));

        sp_accounts_supply -= account.sp_amount;

        FC_ASSERT(sp_accounts_supply.amount >= (share_value_type)0, "Invalid 'steemit_bounty_accounts_supply'.");
    }

    FC_ASSERT(sp_accounts_supply.amount == (share_value_type)0,
              "'steemit_bounty_accounts_supply' must be sum of all steemit_bounty_accounts supply.");

    for (auto& account : genesis_state.steemit_bounty_accounts)
    {
        const auto& account_obj = account_service.get_account(account.name);

        account_service.increase_vesting_shares(account_obj, account.sp_amount);
        dgp_service.update(
            [&](dynamic_global_property_object& props) { props.total_vesting_shares += account.sp_amount; });
    }
}
}
}
}
