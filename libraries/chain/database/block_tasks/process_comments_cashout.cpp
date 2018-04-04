#include <scorum/chain/database/block_tasks/process_comments_cashout.hpp>

#include <scorum/chain/services/reward_fund.hpp>
#include <scorum/chain/services/comment_vote.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/account.hpp>

#include <scorum/chain/schema/scorum_objects.hpp>
#include <scorum/chain/schema/comment_objects.hpp>
#include <scorum/chain/schema/account_objects.hpp>

#include <scorum/chain/util/reward.hpp>

namespace scorum {
namespace chain {
namespace database_ns {

void process_comments_cashout::on_apply(block_task_context& ctx)
{
    data_service_factory_i& services = ctx.services();
    reward_fund_service_i& reward_fund_service = services.reward_fund_service();
    comment_service_i& comment_service = services.comment_service();
#ifdef CLEAR_VOTES
    comment_vote_service_i& comment_vote_service = services.comment_vote_service();
#endif
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();

    // Add all reward funds to the local cache and decay their recent rshares
    reward_fund_service.update([&](reward_fund_object& rfo) {
        fc::microseconds decay_rate = SCORUM_RECENT_RSHARES_DECAY_RATE;
        rfo.recent_claims -= (rfo.recent_claims * (dgp_service.head_block_time() - rfo.last_update).to_seconds())
            / decay_rate.to_seconds();
        rfo.last_update = dgp_service.head_block_time();
    });

    auto comments = comment_service.get_by_cashout_time();

    uint128_t recent_claims = get_recent_claims(ctx, comments);

    /*
     * Payout all comments
     *
     * Each payout follows a similar pattern, but for a different reason.
     * The helper only does token allocation based on curation rewards and the SCR
     * global %, etc.
     *
     * Each context is used by get_rshare_reward to determine what part of each budget
     * the comment is entitled to. Each payout is done
     * against a reward fund state that is snapshotted before all payouts in the block.
     */
    const auto& rf = reward_fund_service.get();

    asset scorum_awarded = asset(0, SCORUM_SYMBOL);
    for (const comment_object& comment : comments)
    {
        if (comment.cashout_time > dgp_service.head_block_time())
            break;

        if (comment.net_rshares > 0)
        {
            util::comment_reward_context comr_ctx;
            comr_ctx.total_reward_shares2 = recent_claims;
            comr_ctx.total_reward_fund_scorum = rf.activity_reward_balance_scr;
            comr_ctx.reward_curve = rf.author_reward_curve;
            comr_ctx.rshares = comment.net_rshares;
            comr_ctx.reward_weight = comment.reward_weight;
            comr_ctx.max_scr = comment.max_accepted_payout;

            scorum_awarded += pay_for_comment(ctx, comment, util::get_rshare_reward(comr_ctx));
        }

        comment_service.update(comment, [&](comment_object& c) {
            /**
             * A payout is only made for positive rshares, negative rshares hang around
             * for the next time this post might get an upvote.
             */
            if (c.net_rshares > 0)
            {
                c.net_rshares = 0;
            }
            c.children_abs_rshares = 0;
            c.abs_rshares = 0;
            c.vote_rshares = 0;
            c.total_vote_weight = 0;
            c.max_cashout_time = fc::time_point_sec::maximum();
            c.cashout_time = fc::time_point_sec::maximum();
            c.last_payout = dgp_service.head_block_time();
        });

        ctx.push_virtual_operation(comment_payout_update_operation(comment.author, fc::to_string(comment.permlink)));

#ifdef CLEAR_VOTES
        auto comment_votes = comment_vote_service.get_by_comment(comment.id);
        for (const comment_vote_object& vote : comment_votes)
        {
            comment_vote_service.remove(vote);
        }
#endif
    }

    // Write the cached fund state back to the database
    reward_fund_service.update([&](reward_fund_object& rfo) {
        rfo.recent_claims = recent_claims;
        rfo.activity_reward_balance_scr -= scorum_awarded;
    });
}

uint128_t process_comments_cashout::get_recent_claims(block_task_context& ctx,
                                                      const comment_service_i::comment_refs_type& comments)
{
    data_service_factory_i& services = ctx.services();
    reward_fund_service_i& reward_fund_service = services.reward_fund_service();
    dynamic_global_property_service_i& dgp_service = services.dynamic_global_property_service();

    //  add all rshares about to be cashed out to the reward funds. This ensures equal satoshi per rshare payment
    const auto& rf = reward_fund_service.get();

    uint128_t recent_claims = rf.recent_claims;
    for (const comment_object& comment : comments)
    {
        if (comment.cashout_time > dgp_service.head_block_time())
            break;

        if (comment.net_rshares > 0)
        {
            recent_claims += util::evaluate_reward_curve(comment.net_rshares.value, rf.author_reward_curve);
        }
    }

    return recent_claims;
}

asset process_comments_cashout::pay_for_comment(block_task_context& ctx,
                                                const comment_object& comment,
                                                const asset& reward)
{
    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();
    comment_service_i& comment_service = services.comment_service();

    try
    {
        if (reward.amount > 0)
        {
            // clang-format off
            asset curation_tokens = asset((uint128_t(reward.amount.value) * SCORUM_CURATION_REWARD_PERCENT / SCORUM_100_PERCENT).to_uint64(), reward.symbol());
            asset author_tokens = reward - curation_tokens;

            author_tokens += pay_curators(ctx, comment, curation_tokens); // curation_tokens can be changed inside pay_curators()

            asset claimed_reward = author_tokens + curation_tokens;

            auto total_beneficiary = asset(0, SCORUM_SYMBOL);
            for (auto& b : comment.beneficiaries)
            {
                asset benefactor_tokens = (author_tokens * b.weight) / SCORUM_100_PERCENT;
                asset sp_created = account_service.create_scorumpower(account_service.get_account(b.account), benefactor_tokens);
                ctx.push_virtual_operation(comment_benefactor_reward_operation(b.account, comment.author, fc::to_string(comment.permlink), sp_created));
                total_beneficiary += benefactor_tokens;
            }

            author_tokens -= total_beneficiary;

            comment_service.update(comment, [&](comment_object& c) {
                c.total_payout_value += author_tokens;
                c.curator_payout_value += curation_tokens;
                c.beneficiary_payout_value += total_beneficiary;
            });

            auto payout_scorum = (author_tokens * comment.percent_scrs) / (2 * SCORUM_100_PERCENT);
            auto vesting_scorum = (author_tokens - payout_scorum);

            const auto& author = account_service.get_account(comment.author);
            account_service.increase_balance(author, payout_scorum);
            asset sp_created = account_service.create_scorumpower(author, vesting_scorum);

            ctx.push_virtual_operation(author_reward_operation(comment.author, fc::to_string(comment.permlink), payout_scorum, sp_created));
            ctx.push_virtual_operation(comment_reward_operation(comment.author, fc::to_string(comment.permlink), claimed_reward));

#ifndef IS_LOW_MEM
            comment_service.update(comment, [&](comment_object& c) { c.author_rewards += author_tokens; });

            account_service.increase_posting_rewards(author, author_tokens);
#endif
            return claimed_reward;
        }

        return asset(0, SCORUM_SYMBOL);
    }
    FC_CAPTURE_AND_RETHROW((comment))
}

asset
process_comments_cashout::pay_curators(block_task_context& ctx, const comment_object& comment, asset& max_rewards)
{
    data_service_factory_i& services = ctx.services();
    account_service_i& account_service = services.account_service();
    comment_vote_service_i& comment_vote_service = services.comment_vote_service();

    try
    {
        uint128_t total_weight(comment.total_vote_weight);
        // edump( (total_weight)(max_rewards) );
        asset unclaimed_rewards = max_rewards;

        if (!comment.allow_curation_rewards)
        {
            // TODO: if allow_curation_rewards == false we loose money, it brings us to break invariants - need to
            // rework
            unclaimed_rewards = asset(0, SCORUM_SYMBOL);
            max_rewards = asset(0, SCORUM_SYMBOL);
        }
        else if (comment.total_vote_weight > 0)
        {
            auto comment_votes = comment_vote_service.get_by_comment_weight_voter(comment.id);
            for (const comment_vote_object& vote : comment_votes)
            {
                uint128_t weight(vote.weight);
                auto claim = asset((max_rewards.amount.value * weight / total_weight).to_uint64(), max_rewards.symbol());
                if (claim.amount > 0) // min_amt is non-zero satoshis
                {
                    unclaimed_rewards -= claim;
                    const auto& voter = account_service.get(vote.voter);
                    auto reward = account_service.create_scorumpower(voter, claim);

                    ctx.push_virtual_operation(
                        curation_reward_operation(voter.name, reward, comment.author, fc::to_string(comment.permlink)));

#ifndef IS_LOW_MEM
                    account_service.increase_curation_rewards(voter, claim);
#endif
                }
            }
        }
        max_rewards -= unclaimed_rewards;

        return unclaimed_rewards;
    }
    FC_CAPTURE_AND_RETHROW()
}
}
}
}


