#pragma once
#include <scorum/protocol/betting/game.hpp>
#include <scorum/protocol/betting/market.hpp>
#include <scorum/protocol/betting/wincase.hpp>

namespace scorum {
namespace protocol {
namespace betting {
void validate_game(const game_type& game, const fc::flat_set<market_type>& markets);
void validate_markets(const fc::flat_set<market_type>& markets);
void validate_market(const market_type& market);
void validate_wincase(const wincase_type& wincase, market_kind market);
}
}
}