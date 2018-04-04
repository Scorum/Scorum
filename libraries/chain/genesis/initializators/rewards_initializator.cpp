#include <scorum/chain/genesis/initializators/rewards_initializator.hpp>
#include <scorum/chain/data_service_factory.hpp>

#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/reward_fund.hpp>
#include <scorum/chain/services/reward_balancer.hpp>
#include <scorum/chain/services/budget.hpp>

#include <scorum/chain/schema/scorum_objects.hpp>
#include <scorum/chain/schema/budget_object.hpp>

#include <scorum/chain/genesis/genesis_state.hpp>

namespace scorum {
namespace chain {
namespace genesis {

void rewards_initializator_impl::on_apply(initializator_context& ctx)
{
    FC_ASSERT(ctx.genesis_state().rewards_supply.symbol() == SCORUM_SYMBOL);

    dynamic_global_property_service_i& dgp_service = ctx.services().dynamic_global_property_service();
    reward_fund_service_i& reward_fund_service = ctx.services().reward_fund_service();
    reward_service_i& reward_service = ctx.services().reward_service();
    budget_service_i& budget_service = ctx.services().budget_service();

    FC_ASSERT(!reward_fund_service.is_exists());

    const auto& post_rf = reward_fund_service.create([&](reward_fund_object& rfo) {
        rfo.last_update = dgp_service.head_block_time();
        rfo.author_reward_curve = curve_id::linear;
        rfo.curation_reward_curve = curve_id::square_root;
    });

    // As a shortcut in payout processing, we use the id as an array index.
    // The IDs must be assigned this way. The assertion is a dummy check to ensure this happens.
    FC_ASSERT(post_rf.id._id == 0);

    FC_ASSERT(!reward_service.is_exists());
    reward_service.create_balancer(asset(0, SCORUM_SYMBOL));

    fc::time_point deadline = dgp_service.get_genesis_time() + fc::days(SCORUM_REWARDS_INITIAL_SUPPLY_PERIOD_IN_DAYS);

    budget_service.create_fund_budget(ctx.genesis_state().rewards_supply, deadline);
}
}
}
}
