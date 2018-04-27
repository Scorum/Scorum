#include <boost/test/unit_test.hpp>

#include <scorum/chain/services/account.hpp>
#include <scorum/chain/services/comment.hpp>
#include <scorum/chain/services/comment_vote.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>

#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/comment_objects.hpp>
#include <scorum/chain/schema/dynamic_global_property_object.hpp>

#include "database_default_integration.hpp"

#include <scorum/rewards_math/formulas.hpp>

using namespace scorum;
using namespace database_fixture;
using namespace scorum::rewards_math;

class vote_apply_base_fixture : public database_default_integration_fixture
{
public:
    vote_apply_base_fixture()
        : account_service(db.account_service())
        , comment_service(db.comment_service())
        , comment_vote_service(db.comment_vote_service())
        , global_property_service(db.dynamic_global_property_service())
    {
        ACTORS((alice)(bob)(sam)(dave))

        private_keys["alice"] = alice_private_key;
        private_keys["bob"] = bob_private_key;
        private_keys["sam"] = sam_private_key;
        private_keys["dave"] = dave_private_key;

        generate_block();

        vest("bob", ASSET_SCR(100e+3));
        vest("sam", ASSET_SCR(100e+3));
        vest("dave", ASSET_SCR(100e+3));

        generate_block();

        auto& alice_comment_op = comment_ops["alice"];
        alice_comment_op.author = "alice";
        alice_comment_op.permlink = "foo";
        alice_comment_op.title = "bar";
        alice_comment_op.body = "foo bar";
        alice_comment_op.parent_permlink = "test";

        auto& bob_comment_op = comment_ops["bob"];
        bob_comment_op.author = "bob";
        bob_comment_op.permlink = "foo";
        bob_comment_op.title = "bar";
        bob_comment_op.body = "foo bar";
        bob_comment_op.parent_permlink = "test";

        auto& sam_comment_op = comment_ops["sam"];
        sam_comment_op.author = "sam";
        sam_comment_op.permlink = "foo";
        sam_comment_op.title = "bar";
        sam_comment_op.body = "foo bar";
        sam_comment_op.parent_permlink = "foo";
        sam_comment_op.parent_author = "alice";
    }

    std::string comment(const std::string& actor)
    {
        BOOST_REQUIRE_NO_THROW(validate_database());

        if (comment_ops[actor].parent_author != SCORUM_ROOT_POST_PARENT_ACCOUNT)
        {
            if (!comment_service.is_exists(comment_ops[actor].parent_author, comment_ops[actor].parent_permlink))
                comment(comment_ops[actor].parent_author);
        }

        signed_transaction tx;

        tx.operations.push_back(comment_ops[actor]);
        tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
        tx.sign(private_keys[actor], db.get_chain_id());

        BOOST_REQUIRE_NO_THROW(db.push_transaction(tx, 0));
        BOOST_REQUIRE_NO_THROW(validate_database());

        generate_block();

        return comment_ops[actor].permlink;
    }

    const account_service_i& account_service;
    const comment_service_i& comment_service;
    const comment_vote_service_i& comment_vote_service;
    const dynamic_global_property_service_i& global_property_service;

    comment_operation alice_comment_op;
    comment_operation bob_comment_op;
    comment_operation sam_comment_op;

    using keys_type = std::map<std::string, private_key_type>;
    keys_type private_keys;

    using comment_ops_type = std::map<std::string, comment_operation>;
    comment_ops_type comment_ops;
};

BOOST_FIXTURE_TEST_SUITE(vote_base_tests, vote_apply_base_fixture)

SCORUM_TEST_CASE(alice_comment_check)
{
    comment("alice");
}

SCORUM_TEST_CASE(bob_comment_check)
{
    comment("bob");
}

SCORUM_TEST_CASE(sam_comment_check)
{
    comment("sam");
}

SCORUM_TEST_CASE(voting_non_existent_comment_check)
{
    comment("alice");

    vote_operation op;
    op.voter = "bob";
    op.author = "sam"; // Sam has not commented yet
    op.permlink = "foo";
    op.weight = (int16_t)100;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["bob"], db.get_chain_id());

    SCORUM_REQUIRE_THROW(db.push_transaction(tx, 0), fc::exception);
}

SCORUM_TEST_CASE(voting_with_zerro_weight_check)
{
    comment("alice");

    vote_operation op;
    op.voter = "bob";
    op.author = "alice";
    op.permlink = "foo";
    op.weight = (int16_t)0;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["bob"], db.get_chain_id());

    SCORUM_REQUIRE_THROW(db.push_transaction(tx, 0), fc::assert_exception);
}

SCORUM_TEST_CASE(voting_with_dust_sp_check)
{
    comment("alice");

    vote_operation op;
    op.voter = "alice"; // Alice has not enough scorum power to vote
    op.author = "alice";
    op.permlink = "foo";
    op.weight = (int16_t)100;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["alice"], db.get_chain_id());

    SCORUM_REQUIRE_THROW(db.push_transaction(tx, 0), fc::assert_exception);
}

SCORUM_TEST_CASE(success_vote_for_100_weight_check)
{
    const auto& alice_comment = comment_service.get("alice", comment("alice"));

    const account_object& bob_vested = account_service.get_account("bob");

    auto old_voting_power = bob_vested.voting_power;

    vote_operation op;
    op.voter = "bob"; // bob has enough scorum power to vote
    op.author = "alice";
    op.permlink = "foo";
    op.weight = (int16_t)100;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["bob"], db.get_chain_id());

    BOOST_REQUIRE_NO_THROW(db.push_transaction(tx, 0));

    BOOST_REQUIRE_NO_THROW(validate_database());

    const auto& comment_vote = comment_vote_service.get(alice_comment.id, bob_vested.id);

    //------------------------------------------------------------------------Bob changes

    // check last_vote_time
    BOOST_REQUIRE_EQUAL(bob_vested.last_vote_time.sec_since_epoch(),
                        global_property_service.head_block_time().sec_since_epoch());

    BOOST_REQUIRE_EQUAL(bob_vested.voting_power,
                        old_voting_power - calculate_used_power(old_voting_power, SCORUM_PERCENT(100),
                                                                SCORUM_VOTING_POWER_DECAY_PERCENT));

    //------------------------------------------------------------Bob comment vote changes

    share_type rshares = (uint128_t(bob_vested.scorumpower.amount.value) * (old_voting_power - bob_vested.voting_power)
                          / SCORUM_100_PERCENT)
                             .to_uint64();

    BOOST_REQUIRE_EQUAL(comment_vote.rshares, rshares);

    //--------------------------------------------------------------Alice comment changes

    BOOST_REQUIRE_EQUAL(alice_comment.abs_rshares.value, rshares.value);

    //
    // net_rshares = rshares, for one vote
    //

    BOOST_REQUIRE_EQUAL(alice_comment.net_rshares.value, rshares.value);

    //
    // vote_rshares = rshares, for one positive vote
    //

    BOOST_REQUIRE_EQUAL(alice_comment.vote_rshares.value, rshares.value);

    BOOST_REQUIRE_EQUAL(alice_comment.cashout_time.sec_since_epoch(),
                        (alice_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

BOOST_AUTO_TEST_SUITE_END()

class vote_apply_for_alice_fixture : public vote_apply_base_fixture
{
public:
    vote_apply_for_alice_fixture()
    {
        vest("alice", ASSET_SCR(100e+3));

        generate_block();
    }

    void vote(const std::string& voter, const std::string& author, int16_t w)
    {
        // comment of author must already exists

        vote_operation op;
        op.voter = voter;
        op.author = author;
        op.permlink = "foo";
        op.weight = w;

        signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
        tx.sign(private_keys[voter], db.get_chain_id());

        BOOST_REQUIRE_NO_THROW(db.push_transaction(tx, 0));
        BOOST_REQUIRE_NO_THROW(validate_database());

        generate_block();
    }
};

BOOST_FIXTURE_TEST_SUITE(vote_logic_tests, vote_apply_for_alice_fixture)

SCORUM_TEST_CASE(reduced_power_for_50_weight_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    auto old_voting_power = alice_vested.voting_power;

    vote_operation op;
    op.voter = "alice";
    op.author = "bob";
    op.permlink = "foo";
    op.weight = (int16_t)(100 / 2);

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["alice"], db.get_chain_id());

    BOOST_REQUIRE_NO_THROW(db.push_transaction(tx, 0));

    BOOST_REQUIRE_NO_THROW(validate_database());

    BOOST_REQUIRE_EQUAL(alice_vested.voting_power,
                        old_voting_power - calculate_used_power(old_voting_power, SCORUM_PERCENT(50),
                                                                SCORUM_VOTING_POWER_DECAY_PERCENT));

    share_type rshares = (uint128_t(alice_vested.scorumpower.amount.value)
                          * (old_voting_power - alice_vested.voting_power) / SCORUM_100_PERCENT)
                             .to_uint64();

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares.value, rshares.value);
}

SCORUM_TEST_CASE(restore_power_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    vote("alice", "bob", 50);

    auto old_abs_rshares = bob_comment.abs_rshares;

    int64_t elapsed_seconds = SCORUM_VOTE_REGENERATION_SECONDS.to_seconds()
        * (SCORUM_100_PERCENT - alice_vested.voting_power) / SCORUM_100_PERCENT;
    generate_blocks(global_property_service.head_block_time() + elapsed_seconds, true);

    vote("alice", "bob", 100);

    // voting_power is total restored

    int64_t used_power
        = calculate_used_power(SCORUM_100_PERCENT, SCORUM_PERCENT(100), SCORUM_VOTING_POWER_DECAY_PERCENT);

    BOOST_REQUIRE_EQUAL(account_service.get_account("alice").voting_power, SCORUM_100_PERCENT - used_power);

    share_type rshares
        = (uint128_t(alice_vested.scorumpower.amount.value) * used_power / SCORUM_100_PERCENT).to_uint64();

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares + rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.cashout_time.sec_since_epoch(),
                        (bob_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

SCORUM_TEST_CASE(negative_vote_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& sam_vested = account_service.get_account("sam");

    vote("alice", "bob", 100); // make not zerro abs_rshares for bob_comment

    auto old_abs_rshares = bob_comment.abs_rshares;

    generate_blocks(global_property_service.head_block_time() + fc::seconds((SCORUM_CASHOUT_WINDOW_SECONDS / 2)), true);

    vote("sam", "bob", -50);

    share_type rshares = (uint128_t(sam_vested.scorumpower.amount.value)
                          * (SCORUM_100_PERCENT - sam_vested.voting_power) / SCORUM_100_PERCENT)
                             .to_uint64();

    BOOST_REQUIRE_EQUAL(sam_vested.voting_power,
                        SCORUM_100_PERCENT - calculate_used_power(SCORUM_100_PERCENT, SCORUM_PERCENT(50),
                                                                  SCORUM_VOTING_POWER_DECAY_PERCENT));

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, old_abs_rshares - rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares + rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.cashout_time.sec_since_epoch(),
                        (bob_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

SCORUM_TEST_CASE(nested_comment_vote_check)
{
    const auto& parent_comment = comment_service.get("alice", comment("alice"));

    // root is itself
    BOOST_REQUIRE_EQUAL(parent_comment.root_comment._id, parent_comment.id._id);

    vote("dave", "alice", 80);

    // create child comment for alice comment

    const auto& sam_comment = comment_service.get("sam", comment("sam"));

    BOOST_REQUIRE_EQUAL(sam_comment.root_comment._id, parent_comment.id._id);

    const account_object& dave_vested = account_service.get_account("dave");

    auto old_abs_rshares = parent_comment.children_abs_rshares;

    generate_blocks(global_property_service.head_block_time() + fc::seconds((SCORUM_CASHOUT_WINDOW_SECONDS / 2)), true);

    vote("dave", "sam", 100);

    share_type rshares = (uint128_t(dave_vested.scorumpower.amount.value)
                          * (SCORUM_100_PERCENT - dave_vested.voting_power) / SCORUM_100_PERCENT)
                             .to_uint64();

    BOOST_REQUIRE_EQUAL(parent_comment.cashout_time.sec_since_epoch(),
                        (parent_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());

    BOOST_REQUIRE_EQUAL(parent_comment.children_abs_rshares, old_abs_rshares + rshares);
}

SCORUM_TEST_CASE(increasing_rshares_for_2_different_voters_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    vote("sam", "bob", 50);

    auto old_abs_rshares = bob_comment.abs_rshares;
    auto old_net_rshares = bob_comment.net_rshares;

    const auto alice_vote_weight = 25;

    auto used_power = calculate_used_power(alice_vested.voting_power, SCORUM_1_PERCENT * alice_vote_weight,
                                           SCORUM_VOTING_POWER_DECAY_PERCENT);
    auto alice_voting_power = alice_vested.voting_power - used_power;
    auto new_rshares = (uint128_t(alice_vested.scorumpower.amount.value) * used_power / SCORUM_100_PERCENT).to_uint64();

    vote("alice", "bob", alice_vote_weight);

    const auto& alice_vote = comment_vote_service.get(bob_comment.id, alice_vested.id);

    BOOST_REQUIRE_EQUAL(alice_vote.rshares, (int64_t)new_rshares);

    BOOST_REQUIRE_EQUAL(alice_vote.vote_percent, alice_vote_weight * SCORUM_1_PERCENT);

    BOOST_REQUIRE_EQUAL(alice_vested.voting_power, alice_voting_power);

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, old_net_rshares + new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares + new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.cashout_time.sec_since_epoch(),
                        (bob_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

SCORUM_TEST_CASE(increasing_rshares_for_2_same_voters_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    vote("alice", "bob", 50);

    const auto& alice_vote = comment_vote_service.get(bob_comment.id, alice_vested.id);

    auto old_abs_rshares = bob_comment.abs_rshares;
    auto old_net_rshares = bob_comment.net_rshares;
    auto old_vote_rshares = alice_vote.rshares;

    const auto alice_second_vote_weight = 35;

    auto alice_voting_power
        = calculate_restoring_power(alice_vested.voting_power, db.head_block_time() + SCORUM_BLOCK_INTERVAL,
                                    db.head_block_time(), SCORUM_VOTE_REGENERATION_SECONDS);
    auto used_power = calculate_used_power(alice_voting_power, SCORUM_1_PERCENT * alice_second_vote_weight,
                                           SCORUM_VOTING_POWER_DECAY_PERCENT);
    alice_voting_power = alice_voting_power - used_power;
    auto new_rshares = (uint128_t(alice_vested.scorumpower.amount.value) * used_power / SCORUM_100_PERCENT).to_uint64();

    wlog("${v}", ("v", account_service.get_account("alice").voting_power));

    vote("alice", "bob", alice_second_vote_weight);

    wlog("${v}", ("v", account_service.get_account("alice").voting_power));

    BOOST_REQUIRE_EQUAL(alice_vote.rshares, (int64_t)new_rshares);

    BOOST_REQUIRE_EQUAL(alice_vote.vote_percent, alice_second_vote_weight * SCORUM_1_PERCENT);

    BOOST_REQUIRE_EQUAL(alice_vested.voting_power, alice_voting_power);

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, old_net_rshares - old_vote_rshares + new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares + new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.cashout_time.sec_since_epoch(),
                        (bob_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

// decreasing_rshares_for_2_different_voters_check is tested by negative_vote_check

SCORUM_TEST_CASE(decreasing_rshares_for_2_same_voters_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    vote("alice", "bob", 50);

    const auto& alice_vote = comment_vote_service.get(bob_comment.id, alice_vested.id);

    auto old_abs_rshares = bob_comment.abs_rshares;
    auto old_net_rshares = bob_comment.net_rshares;
    auto old_vote_rshares = alice_vote.rshares;

    const auto alice_second_vote_weight = -75;

    auto alice_voting_power
        = calculate_restoring_power(alice_vested.voting_power, db.head_block_time() + SCORUM_BLOCK_INTERVAL,
                                    db.head_block_time(), SCORUM_VOTE_REGENERATION_SECONDS);
    auto used_power = calculate_used_power(alice_voting_power, SCORUM_1_PERCENT * std::abs(alice_second_vote_weight),
                                           SCORUM_VOTING_POWER_DECAY_PERCENT);
    alice_voting_power = alice_voting_power - used_power;
    auto new_rshares = (uint128_t(alice_vested.scorumpower.amount.value) * used_power / SCORUM_100_PERCENT).to_uint64();

    vote("alice", "bob", alice_second_vote_weight);

    BOOST_REQUIRE_EQUAL(alice_vote.rshares, -(int64_t)new_rshares);

    BOOST_REQUIRE_EQUAL(alice_vote.vote_percent, alice_second_vote_weight * SCORUM_1_PERCENT);

    BOOST_REQUIRE_EQUAL(alice_vested.voting_power, alice_voting_power);

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, old_net_rshares - old_vote_rshares - new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares + new_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.cashout_time.sec_since_epoch(),
                        (bob_comment.created + SCORUM_CASHOUT_WINDOW_SECONDS).sec_since_epoch());
}

SCORUM_TEST_CASE(removing_vote_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    const account_object& alice_vested = account_service.get_account("alice");

    vote("alice", "bob", -50);

    const auto& alice_vote = comment_vote_service.get(bob_comment.id, alice_vested.id);

    auto old_abs_rshares = bob_comment.abs_rshares;
    auto old_net_rshares = bob_comment.net_rshares;
    auto old_vote_rshares = alice_vote.rshares;

    BOOST_REQUIRE_NE(alice_vote.rshares, (int64_t)0);

    vote("alice", "bob", 0);

    BOOST_REQUIRE_EQUAL(alice_vote.rshares, (int64_t)0);

    BOOST_REQUIRE_EQUAL(alice_vote.vote_percent, (int64_t)0);

    BOOST_REQUIRE_EQUAL(bob_comment.net_rshares, old_net_rshares - old_vote_rshares);

    BOOST_REQUIRE_EQUAL(bob_comment.abs_rshares, old_abs_rshares);
}

SCORUM_TEST_CASE(failure_when_increasing_rshares_within_lockout_period_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    vote("alice", "bob", 90);

    generate_blocks(fc::time_point_sec((bob_comment.cashout_time - SCORUM_UPVOTE_LOCKOUT).sec_since_epoch()
                                       + SCORUM_BLOCK_INTERVAL),
                    true);

    vote_operation op;
    op.voter = "alice";
    op.author = "bob";
    op.permlink = "foo";
    op.weight = (int16_t)100;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["alice"], db.get_chain_id());

    SCORUM_REQUIRE_THROW(db.push_transaction(tx, 0), fc::assert_exception);
}

SCORUM_TEST_CASE(success_when_reducing_rshares_within_lockout_period_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    vote("alice", "bob", 90);

    generate_blocks(fc::time_point_sec((bob_comment.cashout_time - SCORUM_UPVOTE_LOCKOUT).sec_since_epoch()
                                       + SCORUM_BLOCK_INTERVAL),
                    true);

    vote_operation op;
    op.voter = "alice";
    op.author = "bob";
    op.permlink = "foo";
    op.weight = (int16_t)-100;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["alice"], db.get_chain_id());

    BOOST_REQUIRE_NO_THROW(db.push_transaction(tx, 0));
}

SCORUM_TEST_CASE(failure_with_a_new_vote_within_lockout_period_check)
{
    const auto& bob_comment = comment_service.get("bob", comment("bob"));

    vote("alice", "bob", 90);

    generate_blocks(fc::time_point_sec((bob_comment.cashout_time - SCORUM_UPVOTE_LOCKOUT).sec_since_epoch()
                                       + SCORUM_BLOCK_INTERVAL),
                    true);

    vote_operation op;
    op.voter = "dave";
    op.author = "bob";
    op.permlink = "foo";
    op.weight = (int16_t)50;

    signed_transaction tx;
    tx.operations.push_back(op);
    tx.set_expiration(global_property_service.head_block_time() + SCORUM_MAX_TIME_UNTIL_EXPIRATION);
    tx.sign(private_keys["dave"], db.get_chain_id());

    SCORUM_REQUIRE_THROW(db.push_transaction(tx, 0), fc::assert_exception);
}

BOOST_AUTO_TEST_SUITE_END()
