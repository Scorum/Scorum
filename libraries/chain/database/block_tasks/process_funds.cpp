#include <scorum/chain/database/block_tasks/process_funds.hpp>
#include <scorum/chain/database/block_tasks/reward_balance_algorithm.hpp>

#include <scorum/chain/services/account.hpp>
#include <scorum/chain/services/budgets.hpp>
#include <scorum/chain/services/dev_pool.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/reward_balancer.hpp>
#include <scorum/chain/services/reward_funds.hpp>
#include <scorum/chain/services/witness.hpp>
#include <scorum/chain/services/advertising_property.hpp>
#include <scorum/chain/services/hardfork_property.hpp>

#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/dynamic_global_property_object.hpp>
#include <scorum/chain/schema/scorum_objects.hpp>
#include <scorum/chain/schema/dev_committee_object.hpp>
#include <scorum/chain/schema/advertising_property_object.hpp>

#include <scorum/chain/database/block_tasks/process_witness_reward_in_sp_migration.hpp>
#include <scorum/chain/database/budget_management_algorithms.hpp>

#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/range/algorithm_ext/copy_n.hpp>
#include <scorum/utils/range_adaptors.hpp>

#include <vector>

namespace scorum {
namespace chain {
namespace database_ns {

using scorum::protocol::producer_reward_operation;

template <typename ServiceIterfaceType, typename CoeffListType>
asset process_funds::allocate_advertising_cash(ServiceIterfaceType& service,
                                               dynamic_global_property_service_i& dgp_service,
                                               account_service_i& account_service,
                                               const CoeffListType& auction_coefficients,
                                               const budget_type type,
                                               database_virtual_operations_emmiter_i& ctx)
{
    namespace br = boost::range;

    asset total_adv_cash(0, ServiceIterfaceType::object_type::symbol_type);

    advertising_budget_management_algorithm<ServiceIterfaceType> manager(service, dgp_service, account_service);

    auto budgets = service.get_top_budgets(dgp_service.head_block_time());
    std::vector<asset> per_block_list;
    br::transform(budgets, std::back_inserter(per_block_list), [](auto b) { return b.get().per_block; });

    auto valuable_per_block_vec = utils::take_n(per_block_list, auction_coefficients.size() + 1);
    auto auction_bets = calculate_auction_bets(valuable_per_block_vec, auction_coefficients);

    for (size_t i = 0; i < budgets.size(); ++i)
    {
        const auto& budget = budgets[i].get();
        auto budget_owner = budget.owner;
        auto budget_id = budget.id._id;

        auto per_block = manager.allocate_cash(budget);

        auto adv_cash = asset(0, per_block.symbol());
        if (i < auction_bets.size())
            adv_cash = auction_bets[i];

        if (adv_cash.amount > 0)
        {
            total_adv_cash += adv_cash;

            ctx.push_virtual_operation(
                allocate_cash_from_advertising_budget_operation(type, budget_owner, budget_id, adv_cash));
        }

        auto ret_cash = per_block - adv_cash;
        FC_ASSERT(ret_cash.amount >= 0, "cash-back amount cannot be less than zero");
        if (ret_cash.amount > 0)
        {
            manager.cash_back(budget_owner, ret_cash);

            ctx.push_virtual_operation(
                cash_back_from_advertising_budget_to_owner_operation(type, budget_owner, budget_id, ret_cash));
        }
    }

    return total_adv_cash;
}

void process_funds::on_apply(block_task_context& ctx)
{
    if (apply_mainnet_schedule_crutches(ctx))
        return;

    debug_log(ctx.get_block_info(), "process_funds BEGIN");

    data_service_factory_i& services = ctx.services();
    content_reward_scr_service_i& content_reward_service = services.content_reward_scr_service();
    dev_pool_service_i& dev_service = services.dev_pool_service();
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();
    fund_budget_service_i& fund_budget_service = services.fund_budget_service();
    post_budget_service_i& post_budget_service = services.post_budget_service();
    banner_budget_service_i& banner_budget_service = services.banner_budget_service();
    account_service_i& account_service = services.account_service();
    advertising_property_service_i& advertising_property_service = services.advertising_property_service();

    // We don't have inflation.
    // We just get per block reward from original reward fund(4.8M SP)
    // and expect that after initial supply is handed out(fund budget is over) reward budgets will be created by our
    // users(through the purchase of advertising). Advertising budgets are in SCR.

    asset original_fund_reward = asset(0, SP_SYMBOL);
    if (fund_budget_service.is_exists())
    {
        original_fund_reward += fund_budget_management_algorithm(fund_budget_service, dgp_service)
                                    .allocate_cash(fund_budget_service.get());
    }

    distribute_reward(ctx, original_fund_reward); // distribute SP

    asset advertising_budgets_reward = asset(0, SCORUM_SYMBOL);

    advertising_budgets_reward += allocate_advertising_cash(
        post_budget_service, dgp_service, account_service, advertising_property_service.get().auction_post_coefficients,
        budget_type::post, ctx);
    advertising_budgets_reward += allocate_advertising_cash(
        banner_budget_service, dgp_service, account_service,
        advertising_property_service.get().auction_banner_coefficients, budget_type::banner, ctx);

    // 50% of the revenue goes to support and develop the product, namely,
    // towards the company's R&D center.
    asset dev_team_reward = advertising_budgets_reward
        * utils::make_fraction(SCORUM_DEV_TEAM_PER_BLOCK_REWARD_PERCENT, SCORUM_100_PERCENT);
    dev_service.update([&](dev_committee_object& dco) { dco.scr_balance += dev_team_reward; });

    // 50% of revenue is distributed in SCR among users.
    // pass it through reward balancer
    reward_balance_algorithm<content_reward_scr_service_i> balancer(content_reward_service);
    balancer.increase_ballance(advertising_budgets_reward - dev_team_reward);
    asset users_reward = balancer.take_block_reward();

    distribute_reward(ctx, users_reward); // distribute SCR

    debug_log(ctx.get_block_info(), "process_funds END");
}

void process_funds::distribute_reward(block_task_context& ctx, const asset& users_reward)
{
    // clang-format off
    /// 5% of total per block reward(equal to 10% of users only reward) to witness and active sp holder pay
    asset witness_reward = users_reward * utils::make_fraction(SCORUM_WITNESS_PER_BLOCK_REWARD_PERCENT, SCORUM_100_PERCENT);
    asset active_sp_holder_reward = users_reward  * utils::make_fraction(SCORUM_ACTIVE_SP_HOLDERS_PER_BLOCK_REWARD_PERCENT ,SCORUM_100_PERCENT);
    asset content_reward = users_reward - witness_reward - active_sp_holder_reward;
    // clang-format on

    process_witness_reward_in_sp_migration().adjust_witness_reward(ctx, witness_reward);

    FC_ASSERT(content_reward.amount >= 0, "content_reward(${r}) must not be less zero", ("r", content_reward));

    distribute_witness_reward(ctx, witness_reward);

    distribute_active_sp_holders_reward(ctx, active_sp_holder_reward);

    pay_content_reward(ctx, content_reward);
}

void process_funds::distribute_witness_reward(block_task_context& ctx, const asset& witness_reward)
{
    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();
    witness_service_i& witness_service = services.witness_service();

    const auto& cwit = witness_service.get(dgp_service.get().current_witness);

    if (cwit.schedule != witness_object::top20 && cwit.schedule != witness_object::timeshare)
    {
        wlog("Encountered unknown witness type for witness: ${w}", ("w", cwit.owner));
    }

    const auto& witness = account_service.get_account(cwit.owner);

    pay_witness_reward(ctx, witness, witness_reward);

    if (witness_reward.amount != 0)
        ctx.push_virtual_operation(producer_reward_operation(witness.name, witness_reward));
}

void process_funds::distribute_active_sp_holders_reward(block_task_context& ctx, const asset& reward)
{
    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();
    hardfork_property_service_i& hardfork_service = services.hardfork_property_service();

    asset total_reward = get_activity_reward(ctx, reward);

    asset distributed_reward = asset(0, reward.symbol());

    auto active_sp_holders_array = account_service.get_active_sp_holders();
    if (!active_sp_holders_array.empty())
    {
        // distribute
        asset total_sp = std::accumulate(active_sp_holders_array.begin(), active_sp_holders_array.end(),
                                         asset(0, SP_SYMBOL), [&](asset& accumulator, const account_object& account) {
                                             return accumulator += account.vote_reward_competitive_sp;
                                         });

        if (total_sp.amount > 0)
        {
            for (const account_object& account : active_sp_holders_array)
            {
                // It is used SP balance amount of account to calculate reward either in SP or SCR tokens
                asset account_reward
                    = total_reward * utils::make_fraction(account.vote_reward_competitive_sp.amount, total_sp.amount);

                if (hardfork_service.has_hardfork(SCORUM_HARDFORK_0_2))
                {
                    pay_account_pending_reward(ctx, account, account_reward);
                }
                else
                {
                    pay_account_reward(ctx, account, account_reward);
                }

                distributed_reward += account_reward;

                if (!hardfork_service.has_hardfork(SCORUM_HARDFORK_0_2) && account_reward.amount > 0)
                {
                    ctx.push_virtual_operation(active_sp_holders_reward_operation(account.name, account_reward));
                }
            }
        }
    }

    // put undistributed money in special fund
    pay_activity_reward(ctx, total_reward - distributed_reward);
}

void process_funds::pay_account_reward(block_task_context& ctx, const account_object& account, const asset& reward)
{
    if (reward.amount <= 0)
        return;

    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        account_service.increase_balance(account, reward);
    }
    else
    {
        account_service.create_scorumpower(account, reward);
    }
}

void process_funds::pay_account_pending_reward(block_task_context& ctx,
                                               const account_object& account,
                                               const asset& reward)
{
    if (reward.amount <= 0)
        return;

    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        account_service.increase_pending_balance(account, reward);
    }
    else
    {
        account_service.increase_pending_scorumpower(account, reward);
    }
}

void process_funds::pay_witness_reward(block_task_context& ctx, const account_object& witness, const asset& reward)
{
    data_service_factory_i& services = ctx.services();
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        dgp_service.update([&](dynamic_global_property_object& p) { p.total_witness_reward_scr += reward; });
    }
    else
    {
        dgp_service.update([&](dynamic_global_property_object& p) { p.total_witness_reward_sp += reward; });
    }

    pay_account_reward(ctx, witness, reward);
}

void process_funds::pay_content_reward(block_task_context& ctx, const asset& reward)
{
    if (reward.amount <= 0)
        return;

    data_service_factory_i& services = ctx.services();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        content_reward_fund_scr_service_i& reward_fund_service = services.content_reward_fund_scr_service();
        reward_fund_service.update([&](content_reward_fund_scr_object& rfo) { rfo.activity_reward_balance += reward; });
    }
    else
    {
        content_reward_fund_sp_service_i& reward_fund_service = services.content_reward_fund_sp_service();
        reward_fund_service.update([&](content_reward_fund_sp_object& rfo) { rfo.activity_reward_balance += reward; });
    }
}

void process_funds::pay_activity_reward(block_task_context& ctx, const asset& reward)
{
    if (reward.amount <= 0)
        return;

    data_service_factory_i& services = ctx.services();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        voters_reward_scr_service_i& reward_service = services.voters_reward_scr_service();

        reward_balance_algorithm<voters_reward_scr_service_i> balancer(reward_service);
        balancer.increase_ballance(reward);
    }
    else
    {
        voters_reward_sp_service_i& reward_service = services.voters_reward_sp_service();

        reward_balance_algorithm<voters_reward_sp_service_i> balancer(reward_service);
        balancer.increase_ballance(reward);
    }
}

const asset process_funds::get_activity_reward(block_task_context& ctx, const asset& reward)
{
    data_service_factory_i& services = ctx.services();

    if (reward.symbol() == SCORUM_SYMBOL)
    {
        voters_reward_scr_service_i& reward_service = services.voters_reward_scr_service();

        reward_balance_algorithm<voters_reward_scr_service_i> balancer(reward_service);
        return reward + balancer.take_block_reward();
    }
    else
    {
        voters_reward_sp_service_i& reward_service = services.voters_reward_sp_service();

        reward_balance_algorithm<voters_reward_sp_service_i> balancer(reward_service);
        return reward + balancer.take_block_reward();
    }
}

bool process_funds::apply_mainnet_schedule_crutches(block_task_context& ctx)
{
    // We have bug on mainnet: signed_block was applied, but undo_session wasn't pushed, therefore DB state roll backed
    // and witnesses were not rewarded. It leads us to mismatching of schedule for working and newly synced nodes. Next
    // code fixes it.
    if (ctx.block_num() == 1650380 || // fix reward for headshot witness
        ctx.block_num() == 1808664) // fix reward for addit-yury witness
    {
        return true;
    }

    return false;
}
}
}
}
