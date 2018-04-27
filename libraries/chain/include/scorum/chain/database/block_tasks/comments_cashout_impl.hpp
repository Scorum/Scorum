#pragma once

#include <scorum/chain/database/block_tasks/block_tasks.hpp>

#include <scorum/chain/services/comment.hpp>
#include <scorum/chain/services/comment_statistic.hpp>
#include <scorum/chain/services/reward_funds.hpp>
#include <scorum/chain/services/comment_vote.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/account.hpp>
#include <scorum/chain/services/account_blogging_statistic.hpp>

#include <scorum/chain/schema/scorum_objects.hpp>
#include <scorum/chain/schema/comment_objects.hpp>
#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/reward_objects.hpp>

#include <scorum/rewards_math/curve.hpp>
#include <scorum/rewards_math/formulas.hpp>

namespace scorum {
namespace chain {
namespace database_ns {

using scorum::rewards_math::shares_vector_type;
using comment_refs_type = scorum::chain::comment_service_i::comment_refs_type;

class process_comments_cashout_impl
{
public:
    explicit process_comments_cashout_impl(block_task_context& ctx);

    template <typename FundService> void reward(FundService& fund_service, const comment_refs_type& comments)
    {
        using fund_object_type = typename FundService::object_type;

        const auto& rf = fund_service.get();

        if (rf.activity_reward_balance.amount < 1 || comments.empty())
            return;

        shares_vector_type total_rshares = get_total_rshares(comments);
        fc::uint128_t total_claims = rewards_math::calculate_total_claims(
            rf.recent_claims, dgp_service.head_block_time(), rf.last_update, rf.author_reward_curve, total_rshares,
            SCORUM_RECENT_RSHARES_DECAY_RATE);

        auto reward_symbol = rf.activity_reward_balance.symbol();
        asset reward = asset(0, reward_symbol);
        for (const comment_object& comment : comments)
        {
            if (comment.net_rshares > 0)
            {
                auto payout = rewards_math::calculate_payout(
                    comment.net_rshares, total_claims, rf.activity_reward_balance.amount, rf.author_reward_curve,
                    comment.max_accepted_payout.amount, SCORUM_MIN_COMMENT_PAYOUT_SHARE);
                reward += pay_for_comment(comment, asset(payout, reward_symbol));
            }
        }

        // Write the cached fund state back to the database
        fund_service.update([&](fund_object_type& rfo) {
            rfo.recent_claims = total_claims;
            rfo.activity_reward_balance -= reward;
            rfo.last_update = dgp_service.head_block_time();
        });
    }

    void close_comment_payout(const comment_object& comment);

    asset pay_for_comment(const comment_object& comment, const asset& reward);

private:
    shares_vector_type get_total_rshares(const comment_service_i::comment_refs_type& comments);

    asset pay_curators(const comment_object& comment, asset& max_rewards);

    void pay_account(const account_object& recipient, const asset& reward);

    template <class CommentStatisticService>
    void accumulate_comment_statistic(CommentStatisticService& stat_service,
                                      const comment_object& comment,
                                      const asset& total_payout,
                                      const asset& author_tokens,
                                      const asset& curation_tokens,
                                      const asset& total_beneficiary)
    {
        using comment_object_type = typename CommentStatisticService::object_type;

        const auto& stat = stat_service.get(comment.id);
        stat_service.update(stat, [&](comment_object_type& c) {
            c.total_payout_value += total_payout;
            c.author_payout_value += author_tokens;
            c.curator_payout_value += curation_tokens;
            c.beneficiary_payout_value += total_beneficiary;
        });
    }

    void accumulate_statistic(const comment_object& comment,
                              const account_object& author,
                              const asset& author_tokens,
                              const asset& curation_tokens,
                              const asset& total_beneficiary,
                              asset_symbol_type reward_symbol);

    void accumulate_statistic(const account_object& voter, const asset& curation_tokens);

private:
    block_task_context& _ctx;
    dynamic_global_property_service_i& dgp_service;
    account_service_i& account_service;
    account_blogging_statistic_service_i& account_blogging_statistic_service;
    comment_service_i& comment_service;
    comment_statistic_scr_service_i& comment_statistic_scr_service;
    comment_statistic_sp_service_i& comment_statistic_sp_service;
    comment_vote_service_i& comment_vote_service;
};
}
}
}
