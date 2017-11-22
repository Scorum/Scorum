#include <scorum/chain/scorum_evaluator.hpp>
#include <scorum/chain/custom_operation_interpreter.hpp>
#include <scorum/chain/scorum_objects.hpp>
#include <scorum/chain/witness_objects.hpp>
#include <scorum/chain/block_summary_object.hpp>

#include <scorum/chain/util/reward.hpp>

#include <scorum/chain/database.hpp> //replace to dbservice after _temporary_public_impl remove
#include <scorum/chain/dbs_account.hpp>
#include <scorum/chain/dbs_witness.hpp>

#ifndef IS_LOW_MEM
#include <diff_match_patch.h>
#include <boost/locale/encoding_utf.hpp>

using boost::locale::conv::utf_to_utf;

std::wstring utf8_to_wstring(const std::string& str)
{
    return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string wstring_to_utf8(const std::wstring& str)
{
    return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}

#endif

#include <fc/uint128.hpp>
#include <fc/utf8.hpp>

#include <limits>

namespace scorum {
namespace chain {
using fc::uint128_t;

inline void validate_permlink_0_1(const string& permlink)
{
    FC_ASSERT(permlink.size() > SCORUM_MIN_PERMLINK_LENGTH && permlink.size() < SCORUM_MAX_PERMLINK_LENGTH,
              "Permlink is not a valid size.");

    for (auto ch : permlink)
    {
        if (!std::islower(ch) && !std::isdigit(ch) && !(ch == '-'))
        {
            FC_ASSERT(false, "Invalid permlink character: ${ch}", ("ch", std::string(1, ch)));
        }
    }
}

struct strcmp_equal
{
    bool operator()(const shared_string& a, const string& b)
    {
        return a.size() == b.size() || std::strcmp(a.c_str(), b.c_str()) == 0;
    }
};

void witness_update_evaluator::do_apply(const witness_update_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    account_service.check_account_existence(o.owner);

    FC_ASSERT(o.url.size() <= SCORUM_MAX_WITNESS_URL_LENGTH, "URL is too long");
    FC_ASSERT(o.props.account_creation_fee.symbol == SCORUM_SYMBOL);

    const auto& by_witness_name_idx = _db._temporary_public_impl().get_index<witness_index>().indices().get<by_name>();
    auto wit_itr = by_witness_name_idx.find(o.owner);
    if (wit_itr != by_witness_name_idx.end())
    {
        _db._temporary_public_impl().modify(*wit_itr, [&](witness_object& w) {
            from_string(w.url, o.url);
            w.signing_key = o.block_signing_key;
            w.props = o.props;
        });
    }
    else
    {
        _db._temporary_public_impl().create<witness_object>([&](witness_object& w) {
            w.owner = o.owner;
            from_string(w.url, o.url);
            w.signing_key = o.block_signing_key;
            w.created = _db.head_block_time();
            w.props = o.props;
        });
    }
}

void account_create_evaluator::do_apply(const account_create_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& creator = account_service.get_account(o.creator);

    // check creator balance

    FC_ASSERT(creator.balance >= o.fee, "Insufficient balance to create account.",
              ("creator.balance", creator.balance)("required", o.fee));

    // check fee

    const witness_schedule_object& wso = _db.get_witness_schedule_object();
    FC_ASSERT(o.fee >= asset(wso.median_props.account_creation_fee.amount * SCORUM_CREATE_ACCOUNT_WITH_SCORUM_MODIFIER,
                             SCORUM_SYMBOL),
              "Insufficient Fee: ${f} required, ${p} provided.",
              ("f",
               wso.median_props.account_creation_fee
                   * asset(SCORUM_CREATE_ACCOUNT_WITH_SCORUM_MODIFIER, SCORUM_SYMBOL))("p", o.fee));

    // check accounts existence

    account_service.check_account_existence(o.owner.account_auths);

    account_service.check_account_existence(o.active.account_auths);

    account_service.check_account_existence(o.posting.account_auths);

    // write in to DB

    account_service.create_account_by_faucets(o.new_account_name, o.creator, o.memo_key, o.json_metadata, o.owner,
                                             o.active, o.posting, o.fee);
}

void account_create_with_delegation_evaluator::do_apply(const account_create_with_delegation_operation& o)
{
    const auto& props = _db.get_dynamic_global_properties();

    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& creator = account_service.get_account(o.creator);

    const witness_schedule_object& wso = _db.get_witness_schedule_object();

    // check creator balance

    FC_ASSERT(creator.balance >= o.fee, "Insufficient balance to create account.",
              ("creator.balance", creator.balance)("required", o.fee));

    // check delegation fee

    FC_ASSERT(creator.vesting_shares - creator.delegated_vesting_shares
                      - asset(creator.to_withdraw - creator.withdrawn, VESTS_SYMBOL)
                  >= o.delegation,
              "Insufficient vesting shares to delegate to new account.",
              ("creator.vesting_shares", creator.vesting_shares)(
                  "creator.delegated_vesting_shares", creator.delegated_vesting_shares)("required", o.delegation));

    auto target_delegation
        = asset(wso.median_props.account_creation_fee.amount * SCORUM_CREATE_ACCOUNT_WITH_SCORUM_MODIFIER
                    * SCORUM_CREATE_ACCOUNT_DELEGATION_RATIO,
                SCORUM_SYMBOL)
        * props.get_vesting_share_price();

    auto current_delegation
        = asset(o.fee.amount * SCORUM_CREATE_ACCOUNT_DELEGATION_RATIO, SCORUM_SYMBOL) * props.get_vesting_share_price()
        + o.delegation;

    FC_ASSERT(current_delegation >= target_delegation, "Inssufficient Delegation ${f} required, ${p} provided.",
              ("f", target_delegation)("p", current_delegation)(
                  "account_creation_fee", wso.median_props.account_creation_fee)("o.fee", o.fee)("o.delegation",
                                                                                                 o.delegation));

    FC_ASSERT(o.fee >= wso.median_props.account_creation_fee, "Insufficient Fee: ${f} required, ${p} provided.",
              ("f", wso.median_props.account_creation_fee)("p", o.fee));

    // check accounts existence

    account_service.check_account_existence(o.owner.account_auths);

    account_service.check_account_existence(o.active.account_auths);

    account_service.check_account_existence(o.posting.account_auths);

    account_service.create_account_with_delegation(o.new_account_name, o.creator, o.memo_key, o.json_metadata, o.owner,
                                                  o.active, o.posting, o.fee, o.delegation);
}

void account_update_evaluator::do_apply(const account_update_operation& o)
{
    if (o.posting)
        o.posting->validate();

    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& account = account_service.get_account(o.account);
    const auto& account_auth = account_service.get_account_authority(o.account);

    if (o.owner)
    {
#ifndef IS_TEST_NET
        FC_ASSERT(_db.head_block_time() - account_auth.last_owner_update > SCORUM_OWNER_UPDATE_LIMIT,
                  "Owner authority can only be updated once an hour.");
#endif
        account_service.check_account_existence(o.owner->account_auths);

        account_service.update_owner_authority(account, *o.owner);
    }

    if (o.active)
    {
        account_service.check_account_existence(o.active->account_auths);
    }

    if (o.posting)
    {
        account_service.check_account_existence(o.posting->account_auths);
    }

    account_service.update_acount(account, account_auth, o.memo_key, o.json_metadata, o.owner, o.active, o.posting);
}

/**
 *  Because net_rshares is 0 there is no need to update any pending payout calculations or parent posts.
 */
void delete_comment_evaluator::do_apply(const delete_comment_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& auth = account_service.get_account(o.author);
    FC_ASSERT(!(auth.owner_challenged || auth.active_challenged),
              "Operation cannot be processed because account is currently challenged.");

    const auto& comment = _db.get_comment(o.author, o.permlink);
    FC_ASSERT(comment.children == 0, "Cannot delete a comment with replies.");

    FC_ASSERT(comment.cashout_time != fc::time_point_sec::maximum());

    FC_ASSERT(comment.net_rshares <= 0, "Cannot delete a comment with net positive votes.");

    if (comment.net_rshares > 0)
        return;

    const auto& vote_idx
        = _db._temporary_public_impl().get_index<comment_vote_index>().indices().get<by_comment_voter>();

    auto vote_itr = vote_idx.lower_bound(comment_id_type(comment.id));
    while (vote_itr != vote_idx.end() && vote_itr->comment == comment.id)
    {
        const auto& cur_vote = *vote_itr;
        ++vote_itr;
        _db._temporary_public_impl().remove(cur_vote);
    }

    /// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
    if (comment.parent_author != SCORUM_ROOT_POST_PARENT)
    {
        auto parent = &_db.get_comment(comment.parent_author, comment.parent_permlink);
        auto now = _db.head_block_time();
        while (parent)
        {
            _db._temporary_public_impl().modify(*parent, [&](comment_object& p) {
                p.children--;
                p.active = now;
            });
#ifndef IS_LOW_MEM
            if (parent->parent_author != SCORUM_ROOT_POST_PARENT)
                parent = &_db.get_comment(parent->parent_author, parent->parent_permlink);
            else
#endif
                parent = nullptr;
        }
    }

    _db._temporary_public_impl().remove(comment);
}

struct comment_options_extension_visitor
{
    comment_options_extension_visitor(const comment_object& c, dbservice& db)
        : _c(c)
        , _db(db)
        , _account_service(_db.obtain_service<dbs_account>())
    {
    }

    typedef void result_type;

    const comment_object& _c;
    dbservice& _db;
    dbs_account& _account_service;

    void operator()(const comment_payout_beneficiaries& cpb) const
    {
        FC_ASSERT(_c.beneficiaries.size() == 0, "Comment already has beneficiaries specified.");
        FC_ASSERT(_c.abs_rshares == 0, "Comment must not have been voted on before specifying beneficiaries.");

        _db._temporary_public_impl().modify(_c, [&](comment_object& c) {
            for (auto& b : cpb.beneficiaries)
            {
                _account_service.check_account_existence(b.account, "Beneficiary");
                c.beneficiaries.push_back(b);
            }
        });
    }
};

void comment_options_evaluator::do_apply(const comment_options_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& auth = account_service.get_account(o.author);
    FC_ASSERT(!(auth.owner_challenged || auth.active_challenged),
              "Operation cannot be processed because account is currently challenged.");

    const auto& comment = _db.get_comment(o.author, o.permlink);
    if (!o.allow_curation_rewards || !o.allow_votes || o.max_accepted_payout < comment.max_accepted_payout)
        FC_ASSERT(comment.abs_rshares == 0,
                  "One of the included comment options requires the comment to have no rshares allocated to it.");

    FC_ASSERT(comment.allow_curation_rewards >= o.allow_curation_rewards, "Curation rewards cannot be re-enabled.");
    FC_ASSERT(comment.allow_votes >= o.allow_votes, "Voting cannot be re-enabled.");
    FC_ASSERT(comment.max_accepted_payout >= o.max_accepted_payout, "A comment cannot accept a greater payout.");
    FC_ASSERT(comment.percent_scrs >= o.percent_scrs, "A comment cannot accept a greater percent SBD.");

    _db._temporary_public_impl().modify(comment, [&](comment_object& c) {
        c.max_accepted_payout = o.max_accepted_payout;
        c.percent_scrs = o.percent_scrs;
        c.allow_votes = o.allow_votes;
        c.allow_curation_rewards = o.allow_curation_rewards;
    });

    for (auto& e : o.extensions)
    {
        e.visit(comment_options_extension_visitor(comment, _db));
    }
}

void comment_evaluator::do_apply(const comment_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {

        FC_ASSERT(o.title.size() + o.body.size() + o.json_metadata.size(),
                  "Cannot update comment because nothing appears to be changing.");

        const auto& by_permlink_idx
            = _db._temporary_public_impl().get_index<comment_index>().indices().get<by_permlink>();
        auto itr = by_permlink_idx.find(boost::make_tuple(o.author, o.permlink));

        const auto& auth = account_service.get_account(o.author); /// prove it exists

        FC_ASSERT(!(auth.owner_challenged || auth.active_challenged),
                  "Operation cannot be processed because account is currently challenged.");

        comment_id_type id;

        const comment_object* parent = nullptr;
        if (o.parent_author != SCORUM_ROOT_POST_PARENT)
        {
            parent = &_db.get_comment(o.parent_author, o.parent_permlink);
            FC_ASSERT(parent->depth < SCORUM_MAX_COMMENT_DEPTH,
                      "Comment is nested ${x} posts deep, maximum depth is ${y}.",
                      ("x", parent->depth)("y", SCORUM_MAX_COMMENT_DEPTH));
        }

        if (o.json_metadata.size())
            FC_ASSERT(fc::is_utf8(o.json_metadata), "JSON Metadata must be UTF-8");

        auto now = _db.head_block_time();

        if (itr == by_permlink_idx.end())
        {
            if (o.parent_author != SCORUM_ROOT_POST_PARENT)
            {
                FC_ASSERT(_db._temporary_public_impl().get(parent->root_comment).allow_replies,
                          "The parent comment has disabled replies.");
            }

            if (o.parent_author == SCORUM_ROOT_POST_PARENT)
                FC_ASSERT((now - auth.last_root_post) > SCORUM_MIN_ROOT_COMMENT_INTERVAL,
                          "You may only post once every 5 minutes.",
                          ("now", now)("last_root_post", auth.last_root_post));
            else
                FC_ASSERT((now - auth.last_post) > SCORUM_MIN_REPLY_INTERVAL,
                          "You may only comment once every 20 seconds.",
                          ("now", now)("auth.last_post", auth.last_post));

            uint16_t reward_weight = SCORUM_100_PERCENT;

            optional<account_name_type> parent_author;
            if (o.parent_author != SCORUM_ROOT_POST_PARENT)
            {
                parent_author = o.parent_author;
            }

            account_service.add_post(auth, parent_author, now);

            const auto& new_comment = _db._temporary_public_impl().create<comment_object>([&](comment_object& com) {
                validate_permlink_0_1(o.parent_permlink);
                validate_permlink_0_1(o.permlink);

                com.author = o.author;
                from_string(com.permlink, o.permlink);
                com.last_update = _db.head_block_time();
                com.created = com.last_update;
                com.active = com.last_update;
                com.last_payout = fc::time_point_sec::min();
                com.max_cashout_time = fc::time_point_sec::maximum();
                com.reward_weight = reward_weight;

                if (o.parent_author == SCORUM_ROOT_POST_PARENT)
                {
                    com.parent_author = "";
                    from_string(com.parent_permlink, o.parent_permlink);
                    from_string(com.category, o.parent_permlink);
                    com.root_comment = com.id;
                }
                else
                {
                    com.parent_author = parent->author;
                    com.parent_permlink = parent->permlink;
                    com.depth = parent->depth + 1;
                    com.category = parent->category;
                    com.root_comment = parent->root_comment;
                }

                com.cashout_time = com.created + SCORUM_CASHOUT_WINDOW_SECONDS;

#ifndef IS_LOW_MEM
                from_string(com.title, o.title);
                if (o.body.size() < 1024 * 1024 * 128)
                {
                    from_string(com.body, o.body);
                }
                if (fc::is_utf8(o.json_metadata))
                    from_string(com.json_metadata, o.json_metadata);
                else
                    wlog("Comment ${a}/${p} contains invalid UTF-8 metadata", ("a", o.author)("p", o.permlink));
#endif
            });

            id = new_comment.id;

            /// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
            auto now = _db.head_block_time();
            while (parent)
            {
                _db._temporary_public_impl().modify(*parent, [&](comment_object& p) {
                    p.children++;
                    p.active = now;
                });
#ifndef IS_LOW_MEM
                if (parent->parent_author != SCORUM_ROOT_POST_PARENT)
                    parent = &_db.get_comment(parent->parent_author, parent->parent_permlink);
                else
#endif
                    parent = nullptr;
            }
        }
        else // start edit case
        {
            const auto& comment = *itr;

            _db._temporary_public_impl().modify(comment, [&](comment_object& com) {
                com.last_update = _db.head_block_time();
                com.active = com.last_update;
                strcmp_equal equal;

                if (!parent)
                {
                    FC_ASSERT(com.parent_author == account_name_type(), "The parent of a comment cannot be changed.");
                    FC_ASSERT(equal(com.parent_permlink, o.parent_permlink),
                              "The permlink of a comment cannot change.");
                }
                else
                {
                    FC_ASSERT(com.parent_author == o.parent_author, "The parent of a comment cannot be changed.");
                    FC_ASSERT(equal(com.parent_permlink, o.parent_permlink),
                              "The permlink of a comment cannot change.");
                }

#ifndef IS_LOW_MEM
                if (o.title.size())
                    from_string(com.title, o.title);
                if (o.json_metadata.size())
                {
                    if (fc::is_utf8(o.json_metadata))
                        from_string(com.json_metadata, o.json_metadata);
                    else
                        wlog("Comment ${a}/${p} contains invalid UTF-8 metadata", ("a", o.author)("p", o.permlink));
                }

                if (o.body.size())
                {
                    try
                    {
                        diff_match_patch<std::wstring> dmp;
                        auto patch = dmp.patch_fromText(utf8_to_wstring(o.body));
                        if (patch.size())
                        {
                            auto result = dmp.patch_apply(patch, utf8_to_wstring(to_string(com.body)));
                            auto patched_body = wstring_to_utf8(result.first);
                            if (!fc::is_utf8(patched_body))
                            {
                                idump(("invalid utf8")(patched_body));
                                from_string(com.body, fc::prune_invalid_utf8(patched_body));
                            }
                            else
                            {
                                from_string(com.body, patched_body);
                            }
                        }
                        else
                        { // replace
                            from_string(com.body, o.body);
                        }
                    }
                    catch (...)
                    {
                        from_string(com.body, o.body);
                    }
                }
#endif

            });

        } // end EDIT case
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void escrow_transfer_evaluator::do_apply(const escrow_transfer_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {
        const auto& from_account = account_service.get_account(o.from);
        account_service.check_account_existence(o.to);
        account_service.check_account_existence(o.agent);

        FC_ASSERT(o.ratification_deadline > _db.head_block_time(),
                  "The escorw ratification deadline must be after head block time.");
        FC_ASSERT(o.escrow_expiration > _db.head_block_time(), "The escrow expiration must be after head block time.");

        asset scorum_spent = o.scorum_amount;
        if (o.fee.symbol == SCORUM_SYMBOL)
            scorum_spent += o.fee;

        FC_ASSERT(from_account.balance >= scorum_spent,
                  "Account cannot cover SCORUM costs of escrow. Required: ${r} Available: ${a}",
                  ("r", scorum_spent)("a", from_account.balance));

        account_service.decrease_balance(from_account, scorum_spent);

        _db._temporary_public_impl().create<escrow_object>([&](escrow_object& esc) {
            esc.escrow_id = o.escrow_id;
            esc.from = o.from;
            esc.to = o.to;
            esc.agent = o.agent;
            esc.ratification_deadline = o.ratification_deadline;
            esc.escrow_expiration = o.escrow_expiration;
            esc.scorum_balance = o.scorum_amount;
            esc.pending_fee = o.fee;
        });
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void escrow_approve_evaluator::do_apply(const escrow_approve_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {

        const auto& escrow = _db.get_escrow(o.from, o.escrow_id);

        FC_ASSERT(escrow.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).",
                  ("o", o.to)("e", escrow.to));
        FC_ASSERT(escrow.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).",
                  ("o", o.agent)("e", escrow.agent));
        FC_ASSERT(escrow.ratification_deadline >= _db.head_block_time(),
                  "The escrow ratification deadline has passed. Escrow can no longer be ratified.");

        bool reject_escrow = !o.approve;

        if (o.who == o.to)
        {
            FC_ASSERT(!escrow.to_approved, "Account 'to' (${t}) has already approved the escrow.", ("t", o.to));

            if (!reject_escrow)
            {
                _db._temporary_public_impl().modify(escrow, [&](escrow_object& esc) { esc.to_approved = true; });
            }
        }
        if (o.who == o.agent)
        {
            FC_ASSERT(!escrow.agent_approved, "Account 'agent' (${a}) has already approved the escrow.",
                      ("a", o.agent));

            if (!reject_escrow)
            {
                _db._temporary_public_impl().modify(escrow, [&](escrow_object& esc) { esc.agent_approved = true; });
            }
        }

        if (reject_escrow)
        {
            const auto& from_account = account_service.get_account(o.from);
            account_service.increase_balance(from_account, escrow.scorum_balance);
            account_service.increase_balance(from_account, escrow.pending_fee);

            _db._temporary_public_impl().remove(escrow);
        }
        else if (escrow.to_approved && escrow.agent_approved)
        {
            const auto& agent_account = account_service.get_account(o.agent);
            account_service.increase_balance(agent_account, escrow.pending_fee);

            _db._temporary_public_impl().modify(escrow, [&](escrow_object& esc) { esc.pending_fee.amount = 0; });
        }
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void escrow_dispute_evaluator::do_apply(const escrow_dispute_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {
        account_service.check_account_existence(o.from);

        const auto& e = _db.get_escrow(o.from, o.escrow_id);
        FC_ASSERT(_db.head_block_time() < e.escrow_expiration, "Disputing the escrow must happen before expiration.");
        FC_ASSERT(e.to_approved && e.agent_approved,
                  "The escrow must be approved by all parties before a dispute can be raised.");
        FC_ASSERT(!e.disputed, "The escrow is already under dispute.");
        FC_ASSERT(e.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to));
        FC_ASSERT(e.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).",
                  ("o", o.agent)("e", e.agent));

        _db._temporary_public_impl().modify(e, [&](escrow_object& esc) { esc.disputed = true; });
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void escrow_release_evaluator::do_apply(const escrow_release_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {
        account_service.check_account_existence(o.from);
        const auto& receiver_account = account_service.get_account(o.receiver);

        const auto& e = _db.get_escrow(o.from, o.escrow_id);
        FC_ASSERT(e.scorum_balance >= o.scorum_amount,
                  "Release amount exceeds escrow balance. Amount: ${a}, Balance: ${b}",
                  ("a", o.scorum_amount)("b", e.scorum_balance));
        FC_ASSERT(e.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to));
        FC_ASSERT(e.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).",
                  ("o", o.agent)("e", e.agent));
        FC_ASSERT(o.receiver == e.from || o.receiver == e.to, "Funds must be released to 'from' (${f}) or 'to' (${t})",
                  ("f", e.from)("t", e.to));
        FC_ASSERT(e.to_approved && e.agent_approved, "Funds cannot be released prior to escrow approval.");

        // If there is a dispute regardless of expiration, the agent can release funds to either party
        if (e.disputed)
        {
            FC_ASSERT(o.who == e.agent, "Only 'agent' (${a}) can release funds in a disputed escrow.", ("a", e.agent));
        }
        else
        {
            FC_ASSERT(o.who == e.from || o.who == e.to,
                      "Only 'from' (${f}) and 'to' (${t}) can release funds from a non-disputed escrow",
                      ("f", e.from)("t", e.to));

            if (e.escrow_expiration > _db.head_block_time())
            {
                // If there is no dispute and escrow has not expired, either party can release funds to the other.
                if (o.who == e.from)
                {
                    FC_ASSERT(o.receiver == e.to, "Only 'from' (${f}) can release funds to 'to' (${t}).",
                              ("f", e.from)("t", e.to));
                }
                else if (o.who == e.to)
                {
                    FC_ASSERT(o.receiver == e.from, "Only 'to' (${t}) can release funds to 'from' (${t}).",
                              ("f", e.from)("t", e.to));
                }
            }
        }
        // If escrow expires and there is no dispute, either party can release funds to either party.

        account_service.increase_balance(receiver_account, o.scorum_amount);

        _db._temporary_public_impl().modify(e, [&](escrow_object& esc) { esc.scorum_balance -= o.scorum_amount; });

        if (e.scorum_balance.amount == 0)
        {
            _db._temporary_public_impl().remove(e);
        }
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void transfer_evaluator::do_apply(const transfer_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& from_account = account_service.get_account(o.from);
    const auto& to_account = account_service.get_account(o.to);

    account_service.drop_challenged(from_account);

    FC_ASSERT(_db.get_balance(from_account, o.amount.symbol) >= o.amount,
              "Account does not have sufficient funds for transfer.");
    account_service.decrease_balance(from_account, o.amount);
    account_service.increase_balance(to_account, o.amount);
}

void transfer_to_vesting_evaluator::do_apply(const transfer_to_vesting_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& from_account = account_service.get_account(o.from);
    const auto& to_account = o.to.size() ? account_service.get_account(o.to) : from_account;

    FC_ASSERT(_db.get_balance(from_account, SCORUM_SYMBOL) >= o.amount,
              "Account does not have sufficient SCORUM for transfer.");
    account_service.decrease_balance(from_account, o.amount);
    _db.create_vesting(to_account, o.amount);
}

void withdraw_vesting_evaluator::do_apply(const withdraw_vesting_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& account = account_service.get_account(o.account);

    FC_ASSERT(account.vesting_shares >= asset(0, VESTS_SYMBOL),
              "Account does not have sufficient Scorum Power for withdraw.");
    FC_ASSERT(account.vesting_shares - account.delegated_vesting_shares >= o.vesting_shares,
              "Account does not have sufficient Scorum Power for withdraw.");

    if (!account.mined)
    {
        const auto& props = _db.get_dynamic_global_properties();
        const witness_schedule_object& wso = _db.get_witness_schedule_object();

        asset min_vests = wso.median_props.account_creation_fee * props.get_vesting_share_price();
        min_vests.amount.value *= 10;

        FC_ASSERT(
            account.vesting_shares > min_vests || o.vesting_shares.amount == 0,
            "Account registered by another account requires 10x account creation fee worth of Scorum Power before it "
            "can be powered down.");
    }

    if (o.vesting_shares.amount == 0)
    {
        FC_ASSERT(account.vesting_withdraw_rate.amount != 0,
                  "This operation would not change the vesting withdraw rate.");

        account_service.update_withdraw(account, asset(0, VESTS_SYMBOL), time_point_sec::maximum(), 0);
    }
    else
    {

        // SCORUM: We have to decide wether we use 13 weeks vesting period or low it down
        int vesting_withdraw_intervals = SCORUM_VESTING_WITHDRAW_INTERVALS; /// 13 weeks = 1 quarter of a year

        auto new_vesting_withdraw_rate = asset(o.vesting_shares.amount / vesting_withdraw_intervals, VESTS_SYMBOL);

        if (new_vesting_withdraw_rate.amount == 0)
            new_vesting_withdraw_rate.amount = 1;

        FC_ASSERT(account.vesting_withdraw_rate != new_vesting_withdraw_rate,
                  "This operation would not change the vesting withdraw rate.");

        account_service.update_withdraw(account, new_vesting_withdraw_rate,
                                       _db.head_block_time() + fc::seconds(SCORUM_VESTING_WITHDRAW_INTERVAL_SECONDS),
                                       o.vesting_shares.amount);
    }
}

void set_withdraw_vesting_route_evaluator::do_apply(const set_withdraw_vesting_route_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {
        const auto& from_account = account_service.get_account(o.from_account);
        const auto& to_account = account_service.get_account(o.to_account);
        const auto& wd_idx
            = _db._temporary_public_impl().get_index<withdraw_vesting_route_index>().indices().get<by_withdraw_route>();
        auto itr = wd_idx.find(boost::make_tuple(from_account.id, to_account.id));

        if (itr == wd_idx.end())
        {
            FC_ASSERT(o.percent != 0, "Cannot create a 0% destination.");
            FC_ASSERT(from_account.withdraw_routes < SCORUM_MAX_WITHDRAW_ROUTES,
                      "Account already has the maximum number of routes.");

            _db._temporary_public_impl().create<withdraw_vesting_route_object>(
                [&](withdraw_vesting_route_object& wvdo) {
                    wvdo.from_account = from_account.id;
                    wvdo.to_account = to_account.id;
                    wvdo.percent = o.percent;
                    wvdo.auto_vest = o.auto_vest;
                });

            account_service.increase_withdraw_routes(from_account);
        }
        else if (o.percent == 0)
        {
            _db._temporary_public_impl().remove(*itr);

            account_service.decrease_withdraw_routes(from_account);
        }
        else
        {
            _db._temporary_public_impl().modify(*itr, [&](withdraw_vesting_route_object& wvdo) {
                wvdo.from_account = from_account.id;
                wvdo.to_account = to_account.id;
                wvdo.percent = o.percent;
                wvdo.auto_vest = o.auto_vest;
            });
        }

        itr = wd_idx.upper_bound(boost::make_tuple(from_account.id, account_id_type()));
        uint16_t total_percent = 0;

        while (itr->from_account == from_account.id && itr != wd_idx.end())
        {
            total_percent += itr->percent;
            ++itr;
        }

        FC_ASSERT(total_percent <= SCORUM_100_PERCENT,
                  "More than 100% of vesting withdrawals allocated to destinations.");
    }
    FC_CAPTURE_AND_RETHROW()
}

void account_witness_proxy_evaluator::do_apply(const account_witness_proxy_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& account = account_service.get_account(o.account);
    FC_ASSERT(account.proxy != o.proxy, "Proxy must change.");

    FC_ASSERT(account.can_vote, "Account has declined the ability to vote and cannot proxy votes.");

    optional<account_object> proxy;
    if (o.proxy.size())
    {
        proxy = account_service.get_account(o.proxy);
    }
    account_service.update_voting_proxy(account, proxy);
}

void account_witness_vote_evaluator::do_apply(const account_witness_vote_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& voter = account_service.get_account(o.account);
    FC_ASSERT(voter.proxy.size() == 0, "A proxy is currently set, please clear the proxy before voting for a witness.");

    if (o.approve)
        FC_ASSERT(voter.can_vote, "Account has declined its voting rights.");

    const auto& witness = _db.get_witness(o.witness);

    const auto& by_account_witness_idx
        = _db._temporary_public_impl().get_index<witness_vote_index>().indices().get<by_account_witness>();
    auto itr = by_account_witness_idx.find(boost::make_tuple(voter.id, witness.id));

    if (itr == by_account_witness_idx.end())
    {
        FC_ASSERT(o.approve, "Vote doesn't exist, user must indicate a desire to approve witness.");

        FC_ASSERT(voter.witnesses_voted_for < SCORUM_MAX_ACCOUNT_WITNESS_VOTES,
                  "Account has voted for too many witnesses."); // TODO: Remove after hardfork 2

        _db._temporary_public_impl().create<witness_vote_object>([&](witness_vote_object& v) {
            v.witness = witness.id;
            v.account = voter.id;
        });

        _db.adjust_witness_vote(witness, voter.witness_vote_weight());

        account_service.increase_witnesses_voted_for(voter);
    }
    else
    {
        FC_ASSERT(!o.approve, "Vote currently exists, user must indicate a desire to reject witness.");

        _db.adjust_witness_vote(witness, -voter.witness_vote_weight());

        account_service.decrease_witnesses_voted_for(voter);
        _db._temporary_public_impl().remove(*itr);
    }
}

void vote_evaluator::do_apply(const vote_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    try
    {
        const auto& comment = _db.get_comment(o.author, o.permlink);
        const auto& voter = account_service.get_account(o.voter);

        FC_ASSERT(!(voter.owner_challenged || voter.active_challenged),
                  "Operation cannot be processed because the account is currently challenged.");

        FC_ASSERT(voter.can_vote, "Voter has declined their voting rights.");

        if (o.weight > 0)
            FC_ASSERT(comment.allow_votes, "Votes are not allowed on the comment.");

        if (_db.calculate_discussion_payout_time(comment) == fc::time_point_sec::maximum())
        {
#ifndef CLEAR_VOTES
            const auto& comment_vote_idx
                = _db._temporary_public_impl().get_index<comment_vote_index>().indices().get<by_comment_voter>();
            auto itr = comment_vote_idx.find(std::make_tuple(comment.id, voter.id));

            if (itr == comment_vote_idx.end())
                _db._temporary_public_impl().create<comment_vote_object>([&](comment_vote_object& cvo) {
                    cvo.voter = voter.id;
                    cvo.comment = comment.id;
                    cvo.vote_percent = o.weight;
                    cvo.last_update = _db.head_block_time();
                });
            else
                _db._temporary_public_impl().modify(*itr, [&](comment_vote_object& cvo) {
                    cvo.vote_percent = o.weight;
                    cvo.last_update = _db.head_block_time();
                });
#endif
            return;
        }

        const auto& comment_vote_idx
            = _db._temporary_public_impl().get_index<comment_vote_index>().indices().get<by_comment_voter>();
        auto itr = comment_vote_idx.find(std::make_tuple(comment.id, voter.id));

        int64_t elapsed_seconds = (_db.head_block_time() - voter.last_vote_time).to_seconds();

#ifndef IS_TEST_NET
        FC_ASSERT(elapsed_seconds >= SCORUM_MIN_VOTE_INTERVAL_SEC, "Can only vote once every 3 seconds.");
#endif

        int64_t regenerated_power = (SCORUM_100_PERCENT * elapsed_seconds) / SCORUM_VOTE_REGENERATION_SECONDS;
        int64_t current_power = std::min(int64_t(voter.voting_power + regenerated_power), int64_t(SCORUM_100_PERCENT));
        FC_ASSERT(current_power > 0, "Account currently does not have voting power.");

        int64_t abs_weight = abs(o.weight);
        int64_t used_power = (current_power * abs_weight) / SCORUM_100_PERCENT;

        const dynamic_global_property_object& dgpo = _db.get_dynamic_global_properties();

        // used_power = (current_power * abs_weight / SCORUM_100_PERCENT) * (reserve / max_vote_denom)
        // The second multiplication is rounded up as of HF 259
        int64_t max_vote_denom = dgpo.vote_power_reserve_rate * SCORUM_VOTE_REGENERATION_SECONDS / (60 * 60 * 24);
        FC_ASSERT(max_vote_denom > 0);

        used_power = (used_power + max_vote_denom - 1) / max_vote_denom;

        FC_ASSERT(used_power <= current_power, "Account does not have enough power to vote.");

        int64_t abs_rshares
            = ((uint128_t(voter.effective_vesting_shares().amount.value) * used_power) / (SCORUM_100_PERCENT))
                  .to_uint64();

        FC_ASSERT(abs_rshares > SCORUM_VOTE_DUST_THRESHOLD || o.weight == 0,
                  "Voting weight is too small, please accumulate more voting power or scorum power.");

        FC_ASSERT(itr == comment_vote_idx.end() || itr->num_changes != -1,
                  "Cannot vote again on a comment after payout.");

        if (itr == comment_vote_idx.end())
        {
            FC_ASSERT(o.weight != 0, "Vote weight cannot be 0.");
            /// this is the rshares voting for or against the post
            int64_t rshares = o.weight < 0 ? -abs_rshares : abs_rshares;

            if (rshares > 0)
            {
                FC_ASSERT(_db.head_block_time() < comment.cashout_time - SCORUM_UPVOTE_LOCKOUT,
                          "Cannot increase payout within last twelve hours before payout.");
            }

            // used_power /= (50*7); /// a 100% vote means use .28% of voting power which should force users to spread
            // their votes around over 50+ posts day for a week
            // if( used_power == 0 ) used_power = 1;

            account_service.update_voting_power(voter, current_power - used_power);

            /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into
            /// total rshares^2
            fc::uint128_t old_rshares = std::max(comment.net_rshares.value, int64_t(0));
            const auto& root = _db._temporary_public_impl().get(comment.root_comment);

            fc::uint128_t avg_cashout_sec;

            FC_ASSERT(abs_rshares > 0, "Cannot vote with 0 rshares.");

            auto old_vote_rshares = comment.vote_rshares;

            _db._temporary_public_impl().modify(comment, [&](comment_object& c) {
                c.net_rshares += rshares;
                c.abs_rshares += abs_rshares;
                if (rshares > 0)
                    c.vote_rshares += rshares;
                if (rshares > 0)
                    c.net_votes++;
                else
                    c.net_votes--;
            });

            _db._temporary_public_impl().modify(root,
                                                [&](comment_object& c) { c.children_abs_rshares += abs_rshares; });

            fc::uint128_t new_rshares = std::max(comment.net_rshares.value, int64_t(0));

            /// calculate rshares2 value
            new_rshares = util::evaluate_reward_curve(new_rshares);
            old_rshares = util::evaluate_reward_curve(old_rshares);

            uint64_t max_vote_weight = 0;

            /** this verifies uniqueness of voter
             *
             *  cv.weight / c.total_vote_weight ==> % of rshares increase that is accounted for by the vote
             *
             *  W(R) = B * R / ( R + 2S )
             *  W(R) is bounded above by B. B is fixed at 2^64 - 1, so all weights fit in a 64 bit integer.
             *
             *  The equation for an individual vote is:
             *    W(R_N) - W(R_N-1), which is the delta increase of proportional weight
             *
             *  c.total_vote_weight =
             *    W(R_1) - W(R_0) +
             *    W(R_2) - W(R_1) + ...
             *    W(R_N) - W(R_N-1) = W(R_N) - W(R_0)
             *
             *  Since W(R_0) = 0, c.total_vote_weight is also bounded above by B and will always fit in a 64 bit
             *integer.
             *
            **/
            _db._temporary_public_impl().create<comment_vote_object>([&](comment_vote_object& cv) {
                cv.voter = voter.id;
                cv.comment = comment.id;
                cv.rshares = rshares;
                cv.vote_percent = o.weight;
                cv.last_update = _db.head_block_time();

                bool curation_reward_eligible = rshares > 0 && (comment.last_payout == fc::time_point_sec())
                    && comment.allow_curation_rewards && _db.get_curation_rewards_percent(comment) > 0;

                if (curation_reward_eligible)
                {
                    const auto& reward_fund = _db.get_reward_fund(comment);
                    auto curve = reward_fund.curation_reward_curve;
                    uint64_t old_weight = util::evaluate_reward_curve(old_vote_rshares.value, curve).to_uint64();
                    uint64_t new_weight = util::evaluate_reward_curve(comment.vote_rshares.value, curve).to_uint64();
                    cv.weight = new_weight - old_weight;

                    max_vote_weight = cv.weight;

                    /// discount weight by time
                    uint128_t w(max_vote_weight);
                    uint64_t delta_t = std::min(uint64_t((cv.last_update - comment.created).to_seconds()),
                                                uint64_t(SCORUM_REVERSE_AUCTION_WINDOW_SECONDS));

                    w *= delta_t;
                    w /= SCORUM_REVERSE_AUCTION_WINDOW_SECONDS;
                    cv.weight = w.to_uint64();
                }
                else
                {
                    cv.weight = 0;
                }
            });

            if (max_vote_weight) // Optimization
            {
                _db._temporary_public_impl().modify(comment,
                                                    [&](comment_object& c) { c.total_vote_weight += max_vote_weight; });
            }
        }
        else
        {
            FC_ASSERT(itr->num_changes < SCORUM_MAX_VOTE_CHANGES,
                      "Voter has used the maximum number of vote changes on this comment.");

            FC_ASSERT(itr->vote_percent != o.weight, "You have already voted in a similar way.");

            /// this is the rshares voting for or against the post
            int64_t rshares = o.weight < 0 ? -abs_rshares : abs_rshares;

            if (itr->rshares < rshares)
            {
                FC_ASSERT(_db.head_block_time() < comment.cashout_time - SCORUM_UPVOTE_LOCKOUT,
                          "Cannot increase payout within last twelve hours before payout.");
            }

            account_service.update_voting_power(voter, current_power - used_power);

            /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into
            /// total rshares^2
            fc::uint128_t old_rshares = std::max(comment.net_rshares.value, int64_t(0));
            const auto& root = _db._temporary_public_impl().get(comment.root_comment);

            fc::uint128_t avg_cashout_sec;

            _db._temporary_public_impl().modify(comment, [&](comment_object& c) {
                c.net_rshares -= itr->rshares;
                c.net_rshares += rshares;
                c.abs_rshares += abs_rshares;

                /// TODO: figure out how to handle remove a vote (rshares == 0 )
                if (rshares > 0 && itr->rshares < 0)
                    c.net_votes += 2;
                else if (rshares > 0 && itr->rshares == 0)
                    c.net_votes += 1;
                else if (rshares == 0 && itr->rshares < 0)
                    c.net_votes += 1;
                else if (rshares == 0 && itr->rshares > 0)
                    c.net_votes -= 1;
                else if (rshares < 0 && itr->rshares == 0)
                    c.net_votes -= 1;
                else if (rshares < 0 && itr->rshares > 0)
                    c.net_votes -= 2;
            });

            _db._temporary_public_impl().modify(root,
                                                [&](comment_object& c) { c.children_abs_rshares += abs_rshares; });

            fc::uint128_t new_rshares = std::max(comment.net_rshares.value, int64_t(0));

            /// calculate rshares2 value
            new_rshares = util::evaluate_reward_curve(new_rshares);
            old_rshares = util::evaluate_reward_curve(old_rshares);

            _db._temporary_public_impl().modify(comment,
                                                [&](comment_object& c) { c.total_vote_weight -= itr->weight; });

            _db._temporary_public_impl().modify(*itr, [&](comment_vote_object& cv) {
                cv.rshares = rshares;
                cv.vote_percent = o.weight;
                cv.last_update = _db.head_block_time();
                cv.weight = 0;
                cv.num_changes += 1;
            });
        }
    }
    FC_CAPTURE_AND_RETHROW((o))
}

void custom_evaluator::do_apply(const custom_operation& o)
{
}

void custom_json_evaluator::do_apply(const custom_json_operation& o)
{
    dbservice& d = db();
    std::shared_ptr<custom_operation_interpreter> eval = d.get_custom_json_evaluator(o.id);
    if (!eval)
        return;

    try
    {
        eval->apply(o);
    }
    catch (const fc::exception& e)
    {
        if (d.is_producing())
            throw e;
    }
    catch (...)
    {
        elog("Unexpected exception applying custom json evaluator.");
    }
}

void custom_binary_evaluator::do_apply(const custom_binary_operation& o)
{
    dbservice& d = db();

    std::shared_ptr<custom_operation_interpreter> eval = d.get_custom_json_evaluator(o.id);
    if (!eval)
        return;

    try
    {
        eval->apply(o);
    }
    catch (const fc::exception& e)
    {
        if (d.is_producing())
            throw e;
    }
    catch (...)
    {
        elog("Unexpected exception applying custom json evaluator.");
    }
}

void prove_authority_evaluator::do_apply(const prove_authority_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& challenged = account_service.get_account(o.challenged);
    FC_ASSERT(challenged.owner_challenged || challenged.active_challenged,
              "Account is not challeneged. No need to prove authority.");

    account_service.prove_authority(challenged, o.require_owner);
}

void request_account_recovery_evaluator::do_apply(const request_account_recovery_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();
    dbs_witness& witness_service = _db.obtain_service<dbs_witness>();

    const auto& account_to_recover = account_service.get_account(o.account_to_recover);

    if (account_to_recover.recovery_account.length()) // Make sure recovery matches expected recovery account
        FC_ASSERT(account_to_recover.recovery_account == o.recovery_account,
                  "Cannot recover an account that does not have you as there recovery partner.");
    else // Empty string recovery account defaults to top witness
        FC_ASSERT(witness_service.get_top_witness().owner == o.recovery_account,
                  "Top witness must recover an account with no recovery partner.");

    account_service.create_account_recovery(o.account_to_recover, o.new_owner_authority);
}

void recover_account_evaluator::do_apply(const recover_account_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& account_to_recover = account_service.get_account(o.account_to_recover);

    FC_ASSERT(_db.head_block_time() - account_to_recover.last_account_recovery > SCORUM_OWNER_UPDATE_LIMIT,
              "Owner authority can only be updated once an hour.");

    account_service.submit_account_recovery(account_to_recover, o.new_owner_authority, o.recent_owner_authority);
}

void change_recovery_account_evaluator::do_apply(const change_recovery_account_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    account_service.check_account_existence(o.new_recovery_account); // Simply validate account exists
    const auto& account_to_recover = account_service.get_account(o.account_to_recover);

    account_service.change_recovery_account(account_to_recover, o.new_recovery_account);
}

void decline_voting_rights_evaluator::do_apply(const decline_voting_rights_operation& o)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& account = account_service.get_account(o.account);
    const auto& request_idx
        = _db._temporary_public_impl().get_index<decline_voting_rights_request_index>().indices().get<by_account>();
    auto itr = request_idx.find(account.id);

    if (o.decline)
    {
        FC_ASSERT(itr == request_idx.end(), "Cannot create new request because one already exists.");

        _db._temporary_public_impl().create<decline_voting_rights_request_object>(
            [&](decline_voting_rights_request_object& req) {
                req.account = account.id;
                req.effective_date = _db.head_block_time() + SCORUM_OWNER_AUTH_RECOVERY_PERIOD;
            });
    }
    else
    {
        FC_ASSERT(itr != request_idx.end(), "Cannot cancel the request because it does not exist.");
        _db._temporary_public_impl().remove(*itr);
    }
}

void claim_reward_balance_evaluator::do_apply(const claim_reward_balance_operation& op)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& acnt = account_service.get_account(op.account);

    FC_ASSERT(op.reward_scorum <= acnt.reward_scorum_balance, "Cannot claim that much SCORUM. Claim: ${c} Actual: ${a}",
              ("c", op.reward_scorum)("a", acnt.reward_scorum_balance));
    FC_ASSERT(op.reward_vests <= acnt.reward_vesting_balance, "Cannot claim that much VESTS. Claim: ${c} Actual: ${a}",
              ("c", op.reward_vests)("a", acnt.reward_vesting_balance));

    asset reward_vesting_scorum_to_move = asset(0, SCORUM_SYMBOL);
    if (op.reward_vests == acnt.reward_vesting_balance)
        reward_vesting_scorum_to_move = acnt.reward_vesting_scorum;
    else
        reward_vesting_scorum_to_move
            = asset(((uint128_t(op.reward_vests.amount.value) * uint128_t(acnt.reward_vesting_scorum.amount.value))
                     / uint128_t(acnt.reward_vesting_balance.amount.value))
                        .to_uint64(),
                    SCORUM_SYMBOL);

    account_service.increase_balance(acnt, op.reward_scorum);
    account_service.decrease_reward_balance(acnt, op.reward_scorum);
    account_service.increase_vesting_shares(acnt, op.reward_vests, reward_vesting_scorum_to_move);

    _db._temporary_public_impl().modify(_db.get_dynamic_global_properties(), [&](dynamic_global_property_object& gpo) {
        gpo.total_vesting_shares += op.reward_vests;
        gpo.total_vesting_fund_scorum += reward_vesting_scorum_to_move;

        gpo.pending_rewarded_vesting_shares -= op.reward_vests;
        gpo.pending_rewarded_vesting_scorum -= reward_vesting_scorum_to_move;
    });

    _db.adjust_proxied_witness_votes(acnt, op.reward_vests.amount);
}

void delegate_vesting_shares_evaluator::do_apply(const delegate_vesting_shares_operation& op)
{
    dbs_account& account_service = _db.obtain_service<dbs_account>();

    const auto& delegator = account_service.get_account(op.delegator);
    const auto& delegatee = account_service.get_account(op.delegatee);
    auto delegation = _db._temporary_public_impl().find<vesting_delegation_object, by_delegation>(
        boost::make_tuple(op.delegator, op.delegatee));

    auto available_shares = delegator.vesting_shares - delegator.delegated_vesting_shares
        - asset(delegator.to_withdraw - delegator.withdrawn, VESTS_SYMBOL);

    const auto& wso = _db.get_witness_schedule_object();
    const auto& gpo = _db.get_dynamic_global_properties();
    auto min_delegation
        = asset(wso.median_props.account_creation_fee.amount * 10, SCORUM_SYMBOL) * gpo.get_vesting_share_price();
    auto min_update = wso.median_props.account_creation_fee * gpo.get_vesting_share_price();

    // If delegation doesn't exist, create it
    if (delegation == nullptr)
    {
        FC_ASSERT(available_shares >= op.vesting_shares, "Account does not have enough vesting shares to delegate.");
        FC_ASSERT(op.vesting_shares >= min_delegation, "Account must delegate a minimum of ${v}",
                  ("v", min_delegation));

        _db._temporary_public_impl().create<vesting_delegation_object>([&](vesting_delegation_object& obj) {
            obj.delegator = op.delegator;
            obj.delegatee = op.delegatee;
            obj.vesting_shares = op.vesting_shares;
            obj.min_delegation_time = _db.head_block_time();
        });

        account_service.increase_delegated_vesting_shares(delegator, op.vesting_shares);
        account_service.increase_received_vesting_shares(delegatee, op.vesting_shares);
    }
    // Else if the delegation is increasing
    else if (op.vesting_shares >= delegation->vesting_shares)
    {
        auto delta = op.vesting_shares - delegation->vesting_shares;

        FC_ASSERT(delta >= min_update, "Scorum Power increase is not enough of a difference. min_update: ${min}",
                  ("min", min_update));
        FC_ASSERT(available_shares >= op.vesting_shares - delegation->vesting_shares,
                  "Account does not have enough vesting shares to delegate.");

        account_service.increase_delegated_vesting_shares(delegator, delta);
        account_service.increase_received_vesting_shares(delegatee, delta);

        _db._temporary_public_impl().modify(
            *delegation, [&](vesting_delegation_object& obj) { obj.vesting_shares = op.vesting_shares; });
    }
    // Else the delegation is decreasing
    else /* delegation->vesting_shares > op.vesting_shares */
    {
        auto delta = delegation->vesting_shares - op.vesting_shares;

        if (op.vesting_shares.amount > 0)
        {
            FC_ASSERT(delta >= min_update, "Scorum Power decrease is not enough of a difference. min_update: ${min}",
                      ("min", min_update));
            FC_ASSERT(op.vesting_shares >= min_delegation,
                      "Delegation must be removed or leave minimum delegation amount of ${v}", ("v", min_delegation));
        }
        else
        {
            FC_ASSERT(delegation->vesting_shares.amount > 0,
                      "Delegation would set vesting_shares to zero, but it is already zero");
        }

        _db._temporary_public_impl().create<vesting_delegation_expiration_object>(
            [&](vesting_delegation_expiration_object& obj) {
                obj.delegator = op.delegator;
                obj.vesting_shares = delta;
                obj.expiration
                    = std::max(_db.head_block_time() + SCORUM_CASHOUT_WINDOW_SECONDS, delegation->min_delegation_time);
            });

        account_service.decrease_received_vesting_shares(delegatee, delta);

        if (op.vesting_shares.amount > 0)
        {
            _db._temporary_public_impl().modify(
                *delegation, [&](vesting_delegation_object& obj) { obj.vesting_shares = op.vesting_shares; });
        }
        else
        {
            _db._temporary_public_impl().remove(*delegation);
        }
    }
}
}
} // scorum::chain
