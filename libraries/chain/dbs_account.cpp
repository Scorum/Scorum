#include <scorum/chain/dbs_account.hpp>
#include <scorum/chain/database.hpp>

#include <scorum/chain/account_object.hpp>

namespace scorum {
namespace chain {

dbs_account::dbs_account(database& db)
    : _base_type(db)
{
}

const account_object& dbs_account::get_account(const account_name_type& name) const
{
    try
    {
        return db_impl().get<account_object, by_name>(name);
    }
    FC_CAPTURE_AND_RETHROW((name))
}

const account_authority_object& dbs_account::get_account_authority(const account_name_type& name) const
{
    try
    {
        return db_impl().get<account_authority_object, by_account>(name);
    }
    FC_CAPTURE_AND_RETHROW((name))
}

void dbs_account::check_account_existence(const account_name_type& name,
                                          const optional<const char*>& context_type_name) const
{
    auto acc = db_impl().find<account_object, by_name>(name);
    if (context_type_name.valid())
    {
        FC_ASSERT(acc != nullptr, "\"${1}\" \"${2}\" must exist.", ("1", *context_type_name)("2", name));
    }
    else
    {
        FC_ASSERT(acc != nullptr, "Account \"${1}\" must exist.", ("1", name));
    }
}

void dbs_account::check_account_existence(const account_authority_map& names,
                                          const optional<const char*>& context_type_name) const
{
    for (const auto& a : names)
    {
        check_account_existence(a.first, context_type_name);
    }
}

void dbs_account::create_account_by_faucets(const account_name_type& new_account_name,
                                            const account_name_type& creator_name,
                                            const public_key_type& memo_key,
                                            const string& json_metadata,
                                            const authority& owner,
                                            const authority& active,
                                            const authority& posting,
                                            const asset& fee)
{
    FC_ASSERT(fee.symbol == SCORUM_SYMBOL, "invalid asset type (symbol)");

    const auto& props = db_impl().get_dynamic_global_properties();
    const auto& creator = db_impl().get_account(creator_name);

    db_impl().modify(creator, [&](account_object& c) { c.balance -= fee; });

    const auto& new_account = db_impl().create<account_object>([&](account_object& acc) {
        acc.name = new_account_name;
        acc.memo_key = memo_key;
        acc.created = props.time;
        acc.last_vote_time = props.time;
        acc.mined = false;

        acc.recovery_account = creator_name;

#ifndef IS_LOW_MEM
        from_string(acc.json_metadata, json_metadata);
#endif
    });

    db_impl().create<account_authority_object>([&](account_authority_object& auth) {
        auth.account = new_account_name;
        auth.owner = owner;
        auth.active = active;
        auth.posting = posting;
        auth.last_owner_update = fc::time_point_sec::min();
    });

    if (fee.amount > 0)
        db_impl().create_vesting(new_account, fee);
}

void dbs_account::create_account_with_delegation(const account_name_type& new_account_name,
                                                 const account_name_type& creator_name,
                                                 const public_key_type& memo_key,
                                                 const string& json_metadata,
                                                 const authority& owner,
                                                 const authority& active,
                                                 const authority& posting,
                                                 const asset& fee,
                                                 const asset& delegation,
                                                 const optional<time_point_sec>& now)
{
    FC_ASSERT(fee.symbol == SCORUM_SYMBOL, "invalid asset type (symbol)");
    FC_ASSERT(delegation.symbol == VESTS_SYMBOL, "invalid asset type (symbol)");

    const auto& props = db_impl().get_dynamic_global_properties();
    const auto& creator = db_impl().get_account(creator_name);

    db_impl().modify(creator, [&](account_object& c) {
        c.balance -= fee;
        c.delegated_vesting_shares += delegation;
    });

    const auto& new_account = db_impl().create<account_object>([&](account_object& acc) {
        acc.name = new_account_name;
        acc.memo_key = memo_key;
        acc.created = props.time;
        acc.last_vote_time = props.time;
        acc.mined = false;

        acc.recovery_account = creator_name;

        acc.received_vesting_shares = delegation;

#ifndef IS_LOW_MEM
        from_string(acc.json_metadata, json_metadata);
#endif
    });

    db_impl().create<account_authority_object>([&](account_authority_object& auth) {
        auth.account = new_account_name;
        auth.owner = owner;
        auth.active = active;
        auth.posting = posting;
        auth.last_owner_update = fc::time_point_sec::min();
    });

    if (delegation.amount > 0)
    {
        _time t = _get_now(now);

        db_impl().create<vesting_delegation_object>([&](vesting_delegation_object& vdo) {
            vdo.delegator = creator_name;
            vdo.delegatee = new_account_name;
            vdo.vesting_shares = delegation;
            vdo.min_delegation_time = t + SCORUM_CREATE_ACCOUNT_DELEGATION_TIME;
        });
    }

    if (fee.amount > 0)
        db_impl().create_vesting(new_account, fee);
}

void dbs_account::update_acount(const account_object& account,
                                const account_authority_object& account_authority,
                                const public_key_type& memo_key,
                                const string& json_metadata,
                                const optional<authority>& owner,
                                const optional<authority>& active,
                                const optional<authority>& posting,
                                const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    db_impl().modify(account, [&](account_object& acc) {
        if (memo_key != public_key_type())
            acc.memo_key = memo_key;

        if ((active || owner) && acc.active_challenged)
        {
            acc.active_challenged = false;
            acc.last_active_proved = t;
        }

        acc.last_account_update = t;

#ifndef IS_LOW_MEM
        if (json_metadata.size() > 0)
            from_string(acc.json_metadata, json_metadata);
#endif
    });

    if (active || posting)
    {
        db_impl().modify(account_authority, [&](account_authority_object& auth) {
            if (active)
                auth.active = *active;
            if (posting)
                auth.posting = *posting;
        });
    }
}

void dbs_account::update_owner_authority(const account_object& account,
                                         const authority& owner_authority,
                                         const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    if (db_impl().head_block_num() >= SCORUM_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM)
    {
        db_impl().create<owner_authority_history_object>([&](owner_authority_history_object& hist) {
            hist.account = account.name;
            hist.previous_owner_authority = db_impl().get<account_authority_object, by_account>(account.name).owner;
            hist.last_valid_time = t;
        });
    }

    db_impl().modify(db_impl().get<account_authority_object, by_account>(account.name),
                     [&](account_authority_object& auth) {
                         auth.owner = owner_authority;
                         auth.last_owner_update = t;
                     });
}

void dbs_account::increase_balance(const account_object& account, const asset& scorums)
{
    FC_ASSERT(scorums.symbol == SCORUM_SYMBOL, "invalid asset type (symbol)");
    db_impl().modify(account, [&](account_object& acnt) { acnt.balance += scorums; });
}

void dbs_account::decrease_balance(const account_object& account, const asset& scorums)
{
    increase_balance(account, -scorums);
}

void dbs_account::increase_reward_balance(const account_object& account, const asset& scorums)
{
    FC_ASSERT(scorums.symbol == SCORUM_SYMBOL, "invalid asset type (symbol)");
    db_impl().modify(account, [&](account_object& acnt) { acnt.reward_scorum_balance += scorums; });
}

void dbs_account::decrease_reward_balance(const account_object& account, const asset& scorums)
{
    increase_reward_balance(account, -scorums);
}

void dbs_account::increase_vesting_shares(const account_object& account, const asset& vesting, const asset& scorums)
{
    FC_ASSERT(vesting.symbol == VESTS_SYMBOL, "invalid asset type (symbol)");
    FC_ASSERT(scorums.symbol == SCORUM_SYMBOL, "invalid asset type (symbol)");
    db_impl().modify(account, [&](account_object& a) {
        a.vesting_shares += vesting;
        a.reward_vesting_balance -= vesting;
        a.reward_vesting_scorum -= scorums;
    });
}

void dbs_account::increase_delegated_vesting_shares(const account_object& account, const asset& vesting)
{
    FC_ASSERT(vesting.symbol == VESTS_SYMBOL, "invalid asset type (symbol)");
    db_impl().modify(account, [&](account_object& a) { a.delegated_vesting_shares += vesting; });
}

void dbs_account::increase_received_vesting_shares(const account_object& account, const asset& vesting)
{
    FC_ASSERT(vesting.symbol == VESTS_SYMBOL, "invalid asset type (symbol)");
    db_impl().modify(account, [&](account_object& a) { a.received_vesting_shares += vesting; });
}

void dbs_account::decrease_received_vesting_shares(const account_object& account, const asset& vesting)
{
    increase_received_vesting_shares(account, -vesting);
}

void dbs_account::drop_challenged(const account_object& account, const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    if (account.active_challenged)
    {
        db_impl().modify(account, [&](account_object& a) {
            a.active_challenged = false;
            a.last_active_proved = t;
        });
    }
}

void dbs_account::prove_authority(const account_object& account,
                                  bool require_owner,
                                  const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    db_impl().modify(account, [&](account_object& a) {
        a.active_challenged = false;
        a.last_active_proved = t;
        if (require_owner)
        {
            a.owner_challenged = false;
            a.last_owner_proved = t;
        }
    });
}

void dbs_account::update_withdraw(const account_object& account,
                                  const asset& vesting,
                                  const time_point_sec& next_vesting_withdrawal,
                                  const share_type& to_withdrawn,
                                  const optional<share_type>& withdrawn)
{
    db_impl()._temporary_public_impl().modify(account, [&](account_object& a) {
        a.vesting_withdraw_rate = vesting;
        a.next_vesting_withdrawal = next_vesting_withdrawal;
        a.to_withdraw = to_withdrawn;
        a.withdrawn = (withdrawn.valid()) ? (*withdrawn) : 0;
    });
}

void dbs_account::increase_withdraw_routes(const account_object& account)
{
    db_impl().modify(account, [&](account_object& a) { a.withdraw_routes++; });
}

void dbs_account::decrease_withdraw_routes(const account_object& account)
{
    db_impl().modify(account, [&](account_object& a) { a.withdraw_routes--; });
}

void dbs_account::increase_witnesses_voted_for(const account_object& account)
{
    db_impl().modify(account, [&](account_object& a) { a.witnesses_voted_for++; });
}

void dbs_account::decrease_witnesses_voted_for(const account_object& account)
{
    db_impl().modify(account, [&](account_object& a) { a.witnesses_voted_for--; });
}

void dbs_account::add_post(const account_object& author_account,
                           const optional<account_name_type>& parent_author_name,
                           const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    db_impl().modify(author_account, [&](account_object& a) {
        if (!parent_author_name.valid())
        {
            a.last_root_post = t;
            a.post_bandwidth = uint32_t(author_account.post_bandwidth);
        }
        a.last_post = t;
        a.post_count++;
    });
}

void dbs_account::update_voting_power(const account_object& account,
                                      uint16_t voting_power,
                                      const optional<time_point_sec>& now)
{
    _time t = _get_now(now);
    db_impl().modify(account, [&](account_object& a) {
        a.voting_power = voting_power;
        a.last_vote_time = t;
    });
}

void dbs_account::create_account_recovery(const account_name_type& account_to_recover,
                                          const authority& new_owner_authority,
                                          const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    const auto& recovery_request_idx
        = db_impl().get_index<account_recovery_request_index>().indices().get<by_account>();
    auto request = recovery_request_idx.find(account_to_recover);

    if (request == recovery_request_idx.end()) // New Request
    {
        FC_ASSERT(!new_owner_authority.is_impossible(), "Cannot recover using an impossible authority.");
        FC_ASSERT(new_owner_authority.weight_threshold, "Cannot recover using an open authority.");

        check_account_existence(new_owner_authority.account_auths);

        db_impl().create<account_recovery_request_object>([&](account_recovery_request_object& req) {
            req.account_to_recover = account_to_recover;
            req.new_owner_authority = new_owner_authority;
            req.expires = t + SCORUM_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
        });
    }
    else if (new_owner_authority.weight_threshold == 0) // Cancel Request if authority is open
    {
        db_impl().remove(*request);
    }
    else // Change Request
    {
        FC_ASSERT(!new_owner_authority.is_impossible(), "Cannot recover using an impossible authority.");

        check_account_existence(new_owner_authority.account_auths);

        db_impl().modify(*request, [&](account_recovery_request_object& req) {
            req.new_owner_authority = new_owner_authority;
            req.expires = t + SCORUM_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
        });
    }
}

void dbs_account::submit_account_recovery(const account_object& account_to_recover,
                                          const authority& new_owner_authority,
                                          const authority& recent_owner_authority,
                                          const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    const auto& recovery_request_idx
        = db_impl().get_index<account_recovery_request_index>().indices().get<by_account>();
    auto request = recovery_request_idx.find(account_to_recover.name);

    FC_ASSERT(request != recovery_request_idx.end(), "There are no active recovery requests for this account.");
    FC_ASSERT(request->new_owner_authority == new_owner_authority,
              "New owner authority does not match recovery request.");

    const auto& recent_auth_idx = db_impl().get_index<owner_authority_history_index>().indices().get<by_account>();
    auto hist = recent_auth_idx.lower_bound(account_to_recover.name);
    bool found = false;

    while (hist != recent_auth_idx.end() && hist->account == account_to_recover.name && !found)
    {
        found = hist->previous_owner_authority == recent_owner_authority;
        if (found)
            break;
        ++hist;
    }

    FC_ASSERT(found, "Recent authority not found in authority history.");

    db_impl().remove(*request); // Remove first, update_owner_authority may invalidate iterator
    update_owner_authority(account_to_recover, new_owner_authority);
    db_impl().modify(account_to_recover, [&](account_object& a) { a.last_account_recovery = t; });
}

void dbs_account::change_recovery_account(const account_object& account_to_recover,
                                          const account_name_type& new_recovery_account_name,
                                          const optional<time_point_sec>& now)
{
    _time t = _get_now(now);

    const auto& change_recovery_idx
        = db_impl().get_index<change_recovery_account_request_index>().indices().get<by_account>();
    auto request = change_recovery_idx.find(account_to_recover.name);

    if (request == change_recovery_idx.end()) // New request
    {
        db_impl().create<change_recovery_account_request_object>([&](change_recovery_account_request_object& req) {
            req.account_to_recover = account_to_recover.name;
            req.recovery_account = new_recovery_account_name;
            req.effective_on = t + SCORUM_OWNER_AUTH_RECOVERY_PERIOD;
        });
    }
    else if (account_to_recover.recovery_account != new_recovery_account_name) // Change existing request
    {
        db_impl().modify(*request, [&](change_recovery_account_request_object& req) {
            req.recovery_account = new_recovery_account_name;
            req.effective_on = t + SCORUM_OWNER_AUTH_RECOVERY_PERIOD;
        });
    }
    else // Request exists and changing back to current recovery account
    {
        db_impl().remove(*request);
    }
}

void dbs_account::update_voting_proxy(const account_object& account, const optional<account_object>& proxy_account)
{
    /// remove all current votes
    std::array<share_type, SCORUM_MAX_PROXY_RECURSION_DEPTH + 1> delta;
    delta[0] = -account.vesting_shares.amount;
    for (int i = 0; i < SCORUM_MAX_PROXY_RECURSION_DEPTH; ++i)
        delta[i + 1] = -account.proxied_vsf_votes[i];

    db_impl().adjust_proxied_witness_votes(account, delta);

    if (proxy_account.valid())
    {
        flat_set<account_id_type> proxy_chain({ account.id, (*proxy_account).id });
        proxy_chain.reserve(SCORUM_MAX_PROXY_RECURSION_DEPTH + 1);

        /// check for proxy loops and fail to update the proxy if it would create a loop
        auto cprox = &(*proxy_account);
        while (cprox->proxy.size() != 0)
        {
            const auto next_proxy = get_account(cprox->proxy);
            FC_ASSERT(proxy_chain.insert(next_proxy.id).second, "This proxy would create a proxy loop.");
            cprox = &next_proxy;
            FC_ASSERT(proxy_chain.size() <= SCORUM_MAX_PROXY_RECURSION_DEPTH, "Proxy chain is too long.");
        }

        /// clear all individual vote records
        db_impl().clear_witness_votes(account);

        db_impl().modify(account, [&](account_object& a) { a.proxy = (*proxy_account).name; });

        /// add all new votes
        for (int i = 0; i <= SCORUM_MAX_PROXY_RECURSION_DEPTH; ++i)
            delta[i] = -delta[i];
        db_impl().adjust_proxied_witness_votes(account, delta);
    }
    else
    { /// we are clearing the proxy which means we simply update the account
        db_impl().modify(account,
                         [&](account_object& a) { a.proxy = account_name_type(SCORUM_PROXY_TO_SELF_ACCOUNT); });
    }
}

time_point_sec dbs_account::_get_now(const optional<time_point_sec>& now)
{
    time_point_sec ret;
    if (now.valid())
    {
        ret = (*now);
    }
    else
    {
        ret = db_impl().head_block_time();
    }
    return ret;
}
}
}
