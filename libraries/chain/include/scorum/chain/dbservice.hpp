#pragma once

#include <scorum/chain/custom_operation_interpreter.hpp>

#include <scorum/chain/dbs_base_impl.hpp>

namespace chainbase {
class database; // for _temporary_public_imp only
}

namespace scorum {
namespace chain {

class dbservice : public dbservice_dbs_factory
{
    typedef dbservice_dbs_factory _base_type;

protected:
    explicit dbservice(database&);

public:
    virtual ~dbservice();

    // TODO: move most of the methods in dbs specific services

    virtual bool is_producing() const = 0;

    virtual const witness_object& get_witness(const account_name_type& name) const = 0;

    virtual const account_object& get_account(const account_name_type& name) const = 0;

    virtual const comment_object& get_comment(const account_name_type& author, const shared_string& permlink) const = 0;

    virtual const comment_object& get_comment(const account_name_type& author, const string& permlink) const = 0;

    virtual const escrow_object& get_escrow(const account_name_type& name, uint32_t escrow_id) const = 0;

    virtual asset get_balance(const account_object& a, asset_symbol_type symbol) const = 0;

    virtual asset get_balance(const string& aname, asset_symbol_type symbol) const = 0;

    virtual const dynamic_global_property_object& get_dynamic_global_properties() const = 0;

    virtual const witness_schedule_object& get_witness_schedule_object() const = 0;

    virtual uint16_t get_curation_rewards_percent(const comment_object& c) const = 0;

    virtual const reward_fund_object& get_reward_fund(const comment_object& c) const = 0;

    virtual time_point_sec head_block_time() const = 0;

    virtual asset create_vesting(const account_object& to_account, asset scorum, bool to_reward_balance = false) = 0;

    /** this updates the votes for witnesses as a result of account voting proxy changing */
    virtual void adjust_proxied_witness_votes(const account_object& a,
                                              const std::array<share_type, SCORUM_MAX_PROXY_RECURSION_DEPTH + 1>& delta,
                                              int depth = 0)
        = 0;

    /** this updates the votes for all witnesses as a result of account VESTS changing */
    virtual void adjust_proxied_witness_votes(const account_object& a, share_type delta, int depth = 0) = 0;

    /** clears all vote records for a particular account but does not update the
    * witness vote totals.  Vote totals should be updated first via a call to
    * adjust_proxied_witness_votes( a, -a.witness_vote_weight() )
    */
    virtual void clear_witness_votes(const account_object& a) = 0;

    /** this updates the vote of a single witness as a result of a vote being added or removed*/
    virtual void adjust_witness_vote(const witness_object& obj, share_type delta) = 0;

    virtual const time_point_sec calculate_discussion_payout_time(const comment_object& comment) const = 0;

    virtual void adjust_reward_balance(const account_object& a, const asset& delta) = 0;

    virtual std::shared_ptr<custom_operation_interpreter> get_custom_json_evaluator(const string& id) = 0;

    // for TODO only:
    chainbase::database& _temporary_public_impl();
};
}
}
