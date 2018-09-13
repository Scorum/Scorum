#include <boost/test/unit_test.hpp>

#include "budget_check_common.hpp"

namespace budget_starting_tests {

using namespace budget_check_common;

class budget_starting_tests_fixture : public budget_check_fixture
{
public:
    budget_starting_tests_fixture()
        : account_service(db.account_service())
        , development_committee_service(db.development_committee_service())
        , alice("alice")
        , bob("bob")
    {
        actor(initdelegate).create_account(alice);
        actor(initdelegate).give_scr(alice, BUDGET_BALANCE_DEFAULT * 100);
        actor(initdelegate).create_account(bob);
        actor(initdelegate).give_scr(bob, BUDGET_BALANCE_DEFAULT * 200);

        budget_balance = account_service.get_account(alice.name).balance / 10;
        budget_start = db.head_block_time();
        budget_deadline = budget_start + fc::hours(1);
    }

    account_service_i& account_service;
    development_committee_service_i& development_committee_service;

    const fc::microseconds budget_start_interval = fc::seconds(SCORUM_BLOCK_INTERVAL * 22);

    asset budget_balance;
    fc::time_point_sec budget_start;
    fc::time_point_sec budget_deadline;

    Actor alice;
    Actor bob;
};

BOOST_FIXTURE_TEST_SUITE(budget_starting_check, budget_starting_tests_fixture)

SCORUM_TEST_CASE(sequence_alocation_by_start_check)
{
    create_budget(alice, budget_type::post, budget_balance, budget_start, budget_deadline);
    auto start_second_alice_budget = budget_start + budget_start_interval;
    create_budget(alice, budget_type::post, budget_balance, start_second_alice_budget, budget_deadline);
    auto start_third_alice_budget = start_second_alice_budget + budget_start_interval;
    create_budget(alice, budget_type::post, budget_balance, start_third_alice_budget, budget_deadline);

    generate_block();

    BOOST_CHECK_LT(post_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_EQUAL(post_budget_service.get(1).balance, budget_balance);
    BOOST_CHECK_EQUAL(post_budget_service.get(2).balance, budget_balance);

    generate_blocks(start_second_alice_budget, false);

    BOOST_CHECK_LT(post_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_LT(post_budget_service.get(1).balance, budget_balance);
    BOOST_CHECK_EQUAL(post_budget_service.get(2).balance, budget_balance);

    generate_blocks(start_third_alice_budget, false);

    BOOST_CHECK_LT(post_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_LT(post_budget_service.get(1).balance, budget_balance);
    BOOST_CHECK_LT(post_budget_service.get(2).balance, budget_balance);
}

SCORUM_TEST_CASE(alocation_by_start_for_different_owners_check)
{
    create_budget(alice, budget_type::post, budget_balance, budget_start, budget_deadline);
    create_budget(alice, budget_type::banner, budget_balance, budget_start + budget_start_interval, budget_deadline);
    auto start_bob_budgets_group = budget_start + budget_start_interval + budget_start_interval;
    create_budget(bob, budget_type::post, budget_balance, start_bob_budgets_group, budget_deadline);
    create_budget(bob, budget_type::banner, budget_balance, start_bob_budgets_group + budget_start_interval,
                  budget_deadline);

    generate_blocks(budget_start + budget_start_interval, false);

    BOOST_CHECK_LT(post_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_LT(banner_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_EQUAL(post_budget_service.get(1).balance, budget_balance);
    BOOST_CHECK_EQUAL(banner_budget_service.get(1).balance, budget_balance);

    generate_blocks(start_bob_budgets_group + budget_start_interval, false);

    BOOST_CHECK_LT(post_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_LT(banner_budget_service.get(0).balance, budget_balance);
    BOOST_CHECK_LT(post_budget_service.get(1).balance, budget_balance);
    BOOST_CHECK_LT(banner_budget_service.get(1).balance, budget_balance);
}

BOOST_AUTO_TEST_SUITE_END()
}
