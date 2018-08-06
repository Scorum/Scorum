#include <scorum/chain/betting/betting_math.hpp>

namespace scorum {
namespace chain {
namespace betting {

using scorum::protocol::odds_fraction_type;

asset calculate_gain(const asset& bet_stake, const odds& bet_odds)
{
    FC_ASSERT(bet_stake.symbol() == SCORUM_SYMBOL, "Invalid sumbol for stake");
    auto result = bet_stake;
    result *= bet_odds;
    result -= bet_stake;
    return std::max(result, asset(1, result.symbol()));
}

matched_stake_type
calculate_matched_stake(const asset& bet1_stake, const asset& bet2_stake, const odds& bet1_odds, const odds& bet2_odds)
{
    FC_ASSERT(bet1_stake.symbol() == SCORUM_SYMBOL && bet1_stake.symbol() == bet2_stake.symbol(),
              "Invalid sumbol for stake");
    FC_ASSERT((odds_fraction_type)bet1_odds == bet2_odds.inverted()
                  && (odds_fraction_type)bet2_odds == bet1_odds.inverted(),
              "Odds for bet 1 ans bet 2 must make 1 in summ of it's probability");

    matched_stake_type result;

    result.potential_bet1_result = bet1_stake * bet1_odds;
    result.potential_bet2_result = bet2_stake * bet2_odds;

    if (result.potential_bet1_result > result.potential_bet2_result)
    {
        auto matched_stake = result.potential_bet2_result;
        matched_stake *= odds_fraction_type(bet1_odds).coup();
        result.matched_stake = std::max(matched_stake, asset(1, matched_stake.symbol()));
    }
    else if (result.potential_bet1_result < result.potential_bet2_result)
    {
        auto matched_stake = result.potential_bet1_result;
        matched_stake *= odds_fraction_type(bet2_odds).coup();
        result.matched_stake = std::max(matched_stake, asset(1, matched_stake.symbol()));
    }
    else
    {
        result.matched_stake = bet1_stake;
    }

    return result;
}
}
}
}