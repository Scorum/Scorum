#include <boost/test/unit_test.hpp>

#include <boost/uuid/uuid_generators.hpp>

#include <scorum/chain/database/database_virtual_operations.hpp>

#include <scorum/chain/betting/betting_matcher.hpp>

#include <scorum/chain/dba/db_accessor_factory.hpp>

#include <scorum/chain/schema/account_objects.hpp>
#include <scorum/chain/schema/bet_objects.hpp>
#include <scorum/chain/schema/game_object.hpp>
#include <scorum/chain/schema/dynamic_global_property_object.hpp>

#include <scorum/chain/dba/db_accessor.hpp>

#include <scorum/protocol/types.hpp>

#include "detail.hpp"
#include "db_mock.hpp"
#include "defines.hpp"
#include "hippomocks.h"

namespace betting_matcher_tests {

using namespace boost::uuids;

using namespace scorum::chain;
using namespace scorum::protocol;

struct no_bets_fixture
{
private:
    db_mock db;
    size_t _counter = 0;

public:
    MockRepository mocks;
    database_virtual_operations_emmiter_i* vops_emiter = mocks.Mock<database_virtual_operations_emmiter_i>();

    dba::db_accessor<pending_bet_object> pending_dba;
    dba::db_accessor<matched_bet_object> matched_dba;
    dba::db_accessor<dynamic_global_property_object> dprop_dba;

    betting_matcher matcher;

    no_bets_fixture()
        : pending_dba(dba::db_accessor<pending_bet_object>(db))
        , matched_dba(dba::db_accessor<matched_bet_object>(db))
        , dprop_dba(dba::db_accessor<dynamic_global_property_object>(db))
        , matcher(*vops_emiter, pending_dba, matched_dba, dprop_dba)
    {
        setup_db();
        setup_mock();
    }

    void setup_db()
    {
        db.add_index<pending_bet_index>();
        db.add_index<matched_bet_index>();
        db.add_index<game_index>();
        db.add_index<dynamic_global_property_index>();
    }

    void setup_mock()
    {
        mocks.OnCall(vops_emiter, database_virtual_operations_emmiter_i::push_virtual_operation).With(_);
        dprop_dba.create([](auto&) {});
    }

    template <typename T> size_t count() const
    {
        return db.get_index<T, by_id>().size();
    }

    template <typename C> const pending_bet_object& create_bet(C&& constructor)
    {
        ++_counter;

        return db.create<pending_bet_object>([&](pending_bet_object& bet) {
            bet.data.uuid = gen_uuid(boost::lexical_cast<std::string>(_counter));

            constructor(bet);
        });
    }
};

BOOST_AUTO_TEST_SUITE(betting_matcher_tests)

SCORUM_FIXTURE_TEST_CASE(add_bet_with_no_stake_to_the_close_list, no_bets_fixture)
{
    const auto& bet1 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = odds(3, 2);
        bet.data.wincase = total::over({ 1 });
    });

    const auto& bet2 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(0);
        bet.data.odds = bet1.data.odds.inverted();
        bet.data.wincase = create_opposite(bet1.data.wincase);
    });

    auto bets_to_cancel = matcher.match(bet2);

    BOOST_CHECK_EQUAL(1u, bets_to_cancel.size());
}

SCORUM_FIXTURE_TEST_CASE(dont_create_matched_bet_when_stake_is_zero, no_bets_fixture)
{
    const auto& bet1 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = odds(3, 2);
        bet.data.wincase = total::over({ 1 });
    });

    const auto& bet2 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(0);
        bet.data.odds = bet1.data.odds.inverted();
        bet.data.wincase = create_opposite(bet1.data.wincase);
    });

    matcher.match(bet2);

    BOOST_CHECK_EQUAL(0u, count<matched_bet_index>());
}

SCORUM_FIXTURE_TEST_CASE(dont_add_bet_with_no_stake_to_the_close_list, no_bets_fixture)
{
    const auto& bet1 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(0);
        bet.data.odds = odds(3, 2);
        bet.data.wincase = total::over({ 1 });
    });

    auto bets_to_cancel = matcher.match(bet1);

    BOOST_CHECK_EQUAL(0u, bets_to_cancel.size());
}

SCORUM_FIXTURE_TEST_CASE(dont_match_bets_with_the_same_wincase, no_bets_fixture)
{
    const auto& bet1 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = odds(3, 2);
        bet.data.wincase = total::over({ 1 });
    });

    const auto& bet2 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = bet1.data.odds.inverted();
        bet.data.wincase = bet1.data.wincase;
    });

    matcher.match(bet2);

    BOOST_CHECK_EQUAL(0u, count<matched_bet_index>());
}

SCORUM_FIXTURE_TEST_CASE(dont_match_bets_with_the_same_odds, no_bets_fixture)
{
    const auto& bet1 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = odds(3, 2);
        bet.data.wincase = total::over({ 1 });
    });

    const auto& bet2 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);
        bet.data.odds = bet1.data.odds;
        bet.data.wincase = create_opposite(bet1.data.wincase);
    });

    matcher.match(bet2);

    BOOST_CHECK_EQUAL(0u, count<matched_bet_index>());
}

SCORUM_FIXTURE_TEST_CASE(match_with_two_bets, no_bets_fixture)
{
    const auto one_point_five = odds(3, 2);
    const auto total_over_1 = total::over({ 1 });

    create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);

        bet.data.odds = one_point_five;
        bet.data.wincase = total_over_1;
    });

    create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);

        bet.data.odds = one_point_five;
        bet.data.wincase = total_over_1;
    });

    const auto& bet3 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(10);

        bet.data.odds = one_point_five.inverted();
        bet.data.wincase = create_opposite(total_over_1);
    });

    matcher.match(bet3);

    BOOST_CHECK_EQUAL(2u, count<matched_bet_index>());
}

struct two_bets_fixture : public no_bets_fixture
{
public:
    bet_data bet1;
    bet_data bet2;

    two_bets_fixture()
    {
        bet1 = create_bet([&](pending_bet_object& bet) {
                   bet.data.stake = ASSET_SCR(10);
                   bet.data.odds = odds(3, 2);
                   bet.data.wincase = total::over({ 1 });
               }).data;

        bet2 = create_bet([&](pending_bet_object& bet) {
                   bet.data.stake = ASSET_SCR(10);
                   bet.data.odds = bet1.odds.inverted();
                   bet.data.wincase = create_opposite(bet1.wincase);
               }).data;
    }
};

SCORUM_FIXTURE_TEST_CASE(add_fully_matched_bet_to_cancel_list, two_bets_fixture)
{
    auto bets_to_cancel = matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_REQUIRE_EQUAL(1u, bets_to_cancel.size());

    BOOST_CHECK(bets_to_cancel.front().get().data.uuid == bet1.uuid);
}

SCORUM_FIXTURE_TEST_CASE(expect_virtual_operation_call_on_bets_matching, two_bets_fixture)
{
    using namespace scorum::protocol;

    std::vector<operation> ops;
    auto collect_virtual_operation = [&](const operation& o) { ops.push_back(o); };

    mocks.OnCall(vops_emiter, database_virtual_operations_emmiter_i::push_virtual_operation)
        .With(_)
        .Do(collect_virtual_operation);

    matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_REQUIRE_EQUAL(3u, ops.size());
    BOOST_CHECK(ops[0].which() == operation::tag<bet_updated_operation>::value);
    BOOST_CHECK(ops[1].which() == operation::tag<bet_updated_operation>::value);
    BOOST_CHECK(ops[2].which() == operation::tag<bets_matched_operation>::value);
}

SCORUM_FIXTURE_TEST_CASE(create_one_matched_bet, two_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_CHECK_EQUAL(1u, count<matched_bet_index>());
}

SCORUM_FIXTURE_TEST_CASE(check_bets_matched_stake, two_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_CHECK_EQUAL(10u, matched_dba.get_by<by_id>(0u).bet1_data.stake.amount);
    BOOST_CHECK_EQUAL(5u, matched_dba.get_by<by_id>(0u).bet2_data.stake.amount);
}

SCORUM_FIXTURE_TEST_CASE(second_bet_matched_on_five_tockens, two_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_CHECK_EQUAL(5u, pending_dba.get_by<by_uuid>(bet2.uuid).data.stake.amount);
}

SCORUM_FIXTURE_TEST_CASE(first_bet_matched_on_hole_stake, two_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet2.uuid));

    BOOST_CHECK_EQUAL(0u, pending_dba.get_by<by_uuid>(bet1.uuid).data.stake.amount);
}

struct three_bets_fixture : public no_bets_fixture
{
    bet_data bet1;
    bet_data bet2;
    bet_data bet3;

    const odds one_point_five = odds(3, 2);
    const total::over total_over_1 = total::over({ 1 });

    three_bets_fixture()
    {
        setup_mock();

        bet1 = create_bet([&](pending_bet_object& bet) {
                   bet.data.stake = ASSET_SCR(10);

                   bet.data.odds = one_point_five;
                   bet.data.wincase = total_over_1;
               }).data;

        bet2 = create_bet([&](pending_bet_object& bet) {
                   bet.data.stake = ASSET_SCR(10);

                   bet.data.odds = one_point_five;
                   bet.data.wincase = total_over_1;
               }).data;

        bet3 = create_bet([&](pending_bet_object& bet) {
                   bet.data.stake = ASSET_SCR(10);

                   bet.data.odds = odds(2, 1);
                   bet.data.wincase = create_opposite(total_over_1);
               }).data;
    }
};

SCORUM_FIXTURE_TEST_CASE(dont_match_bets_when_odds_dont_match, three_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet3.uuid));

    BOOST_CHECK_EQUAL(0u, count<matched_bet_index>());
}

SCORUM_FIXTURE_TEST_CASE(dont_change_pending_bet_stake_when_matching_dont_occur, three_bets_fixture)
{
    matcher.match(pending_dba.get_by<by_uuid>(bet3.uuid));

    BOOST_CHECK(compare_bet_data(bet1, pending_dba.get_by<by_uuid>(bet1.uuid).data));
    BOOST_CHECK(compare_bet_data(bet2, pending_dba.get_by<by_uuid>(bet2.uuid).data));
    BOOST_CHECK(compare_bet_data(bet3, pending_dba.get_by<by_uuid>(bet3.uuid).data));
}

SCORUM_FIXTURE_TEST_CASE(stop_matching_when_stake_is_spent, three_bets_fixture)
{
    const auto& bet4 = create_bet([&](pending_bet_object& bet) {
        bet.data.stake = ASSET_SCR(1);

        bet.data.odds = one_point_five.inverted();
        bet.data.wincase = create_opposite(total_over_1);
    });

    matcher.match(bet4);

    BOOST_REQUIRE_EQUAL(1u, count<matched_bet_index>());

    BOOST_CHECK(matched_dba.get().bet1_data.uuid == bet1.uuid);
    BOOST_CHECK(matched_dba.get().bet2_data.uuid == bet4.data.uuid);
}

BOOST_AUTO_TEST_SUITE_END()

using uuid_type = scorum::uuid_type;

SCORUM_TEST_CASE(matching_fix_list_from_string_test)
{
    static const std::string str
        = R"([["01000000-0000-0000-0000-000000000000",["02000000-0000-0000-0000-000000000000","03000000-0000-0000-0000-000000000000"]]])";

    auto matched_bets = fc::json::from_string(str).as<matching_fix_list>();

    BOOST_CHECK_EQUAL(str, fc::json::to_string(matched_bets));
}

SCORUM_TEST_CASE(matching_fix_list_to_string_test)
{
    matching_fix_list fix;
    fix.insert(std::make_pair<uuid_type, std::vector<uuid_type>>({ { 1 } }, { { { 2 } }, { { 3 } } }));

    BOOST_CHECK_EQUAL(
        R"([["01000000-0000-0000-0000-000000000000",["02000000-0000-0000-0000-000000000000","03000000-0000-0000-0000-000000000000"]]])",
        fc::json::to_string(fix));
}

SCORUM_TEST_CASE(by_game_uuid_wincase_asc_sorting_test)
{
    db_mock db;
    db.add_index<pending_bet_index>();

    dba::db_accessor<pending_bet_object> pending_dba(db);

    boost::uuids::uuid game_uuid = { { 7 } };

    auto wincase = total::over({ 1000 });

    pending_dba.create([&](pending_bet_object& bet) { //
        bet.data.uuid = { { 1 } };
        bet.game_uuid = game_uuid;
        bet.data.wincase = wincase;
        bet.data.created = fc::time_point_sec::from_iso_string("2018-11-25T12:00:00");
    });

    pending_dba.create([&](pending_bet_object& bet) { //
        bet.data.uuid = { { 3 } };
        bet.game_uuid = game_uuid;
        bet.data.wincase = wincase;
        bet.data.created = fc::time_point_sec::from_iso_string("2018-11-25T12:02:00");
    });

    pending_dba.create([&](pending_bet_object& bet) { //
        bet.data.uuid = { { 2 } };
        bet.game_uuid = game_uuid;
        bet.data.wincase = wincase;
        bet.data.created = fc::time_point_sec::from_iso_string("2018-11-25T12:01:00");
    });

    auto key = std::make_tuple(game_uuid, wincase);

    auto pending_bets = pending_dba.get_range_by<by_game_uuid_wincase_asc>(key);

    std::vector<pending_bet_object> bets(pending_bets.begin(), pending_bets.end());

    BOOST_REQUIRE_EQUAL(3u, bets.size());

    BOOST_CHECK(bets[0].data.uuid == boost::uuids::uuid({ { 1 } }));
    BOOST_CHECK(bets[1].data.uuid == boost::uuids::uuid({ { 2 } }));
    BOOST_CHECK(bets[2].data.uuid == boost::uuids::uuid({ { 3 } }));
}
}
