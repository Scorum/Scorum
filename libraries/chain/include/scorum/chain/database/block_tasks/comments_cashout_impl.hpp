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

#include <boost/range/adaptor/reversed.hpp>
#include <map>

namespace scorum {
namespace chain {
namespace database_ns {

using scorum::rewards_math::shares_vector_type;
using comment_refs_type = scorum::chain::comment_service_i::comment_refs_type;

class process_comments_cashout_impl
{
public:
    struct comment_payout_result
    {
        asset total_claimed_reward;
        asset parent_author_reward;
    };

    explicit process_comments_cashout_impl(block_task_context& ctx);

    template <typename FundService> void update_decreasing_total_claims(FundService& fund_service)
    {
        using fund_object_type = typename FundService::object_type;

        const auto& rf = fund_service.get();

        auto now = dgp_service.head_block_time();

        fc::uint128_t total_claims = rewards_math::calculate_decreasing_total_claims(
            rf.recent_claims, now, rf.last_update, SCORUM_RECENT_RSHARES_DECAY_RATE);

        fund_service.update([&](fund_object_type& rfo) {
            rfo.recent_claims = total_claims;
            rfo.last_update = now;
        });
    }

    template <typename FundService> void reward(FundService& fund_service, const comment_refs_type& comments)
    {
        using fund_object_type = typename FundService::object_type;

        const auto& rf = fund_service.get();

        if (rf.activity_reward_balance.amount < 1 || comments.empty())
            return;

        shares_vector_type total_rshares = get_total_rshares(comments);
        fc::uint128_t total_claims
            = rewards_math::calculate_total_claims(rf.recent_claims, rf.author_reward_curve, total_rshares);

        struct comment_reward
        {
            share_type fund; // reward accrued from fund
            share_type commenting; // reward accrued from children comments
        };

        std::map<account_name_type, comment_reward> comment_rewards;

        fc_ilog(fc::logger::get("fifa"), "---");

        for (const comment_object& comment : comments)
        {
            share_type payout = rewards_math::calculate_payout(
                comment.net_rshares, total_claims, rf.activity_reward_balance.amount, rf.author_reward_curve,
                comment.max_accepted_payout.amount, SCORUM_MIN_COMMENT_PAYOUT_SHARE);

            comment_rewards.emplace(comment.author, comment_reward{ payout, 0 });

            fc_ilog(fc::logger::get("fifa"), "${author}:${payout}", ("author", comment.author)("payout", payout));
        }

        fc_ilog(fc::logger::get("fifa"), "---");

        asset_symbol_type reward_symbol = rf.activity_reward_balance.symbol();
        asset total_reward = asset(0, reward_symbol);

        // newest, with bigger depth comments first
        comment_refs_type comments_with_parents = collect_parents(comments);

        for (const comment_object& comment : comments_with_parents)
        {
            const comment_reward& reward = comment_rewards[comment.author];

            asset fund_reward = asset(reward.fund, reward_symbol);
            asset commenting_reward = asset(reward.commenting, reward_symbol);

            comment_payout_result payout_result = pay_for_comment(comment, fund_reward, commenting_reward);

            fc_ilog(fc::logger::get("fifa"), "result=${parent_author_reward} ${total_claimed_reward}",
                    ("parent_author_reward", payout_result.parent_author_reward)
                    ("total_claimed_reward", payout_result.total_claimed_reward));

            total_reward += payout_result.total_claimed_reward;

            fc_ilog(fc::logger::get("fifa"), "total_reward=${total_reward}", ("total_reward", total_reward));

            // save payout for the parent comment
            comment_rewards[comment.parent_author].commenting += payout_result.parent_author_reward.amount;
        }

        fc_ilog(fc::logger::get("fifa"), "---");

        fund_service.update([&](fund_object_type& rfo) {
            rfo.recent_claims = total_claims;
            rfo.activity_reward_balance -= total_reward;
            rfo.last_update = dgp_service.head_block_time();
        });
    }

    void close_comment_payout(const comment_object& comment);

    comment_payout_result pay_for_comment(const comment_object& comment,
                                          const asset& publication_reward,
                                          const asset& children_comments_reward);

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
                                      const asset& total_beneficiary,
                                      const asset& publication_tokens,
                                      const asset& children_comments_tokens)
    {
        using comment_object_type = typename CommentStatisticService::object_type;

        const auto& stat = stat_service.get(comment.id);
        stat_service.update(stat, [&](comment_object_type& c) {
            c.total_payout_value += total_payout;
            c.author_payout_value += author_tokens;
            c.curator_payout_value += curation_tokens;
            c.beneficiary_payout_value += total_beneficiary;
            c.comment_publication_reward += publication_tokens;
            c.children_comments_reward += children_comments_tokens;
        });
    }

    void accumulate_statistic(const comment_object& comment,
                              const account_object& author,
                              const asset& author_tokens,
                              const asset& curation_tokens,
                              const asset& total_beneficiary,
                              const asset& publication_tokens,
                              const asset& children_comments_tokens,
                              asset_symbol_type reward_symbol);

    void accumulate_statistic(const account_object& voter, const asset& curation_tokens);

    comment_refs_type collect_parents(const comment_refs_type& comments);

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
