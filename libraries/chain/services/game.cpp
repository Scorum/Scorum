#include <scorum/chain/services/game.hpp>
#include <scorum/chain/database/database.hpp>
#include <scorum/chain/services/dynamic_global_property.hpp>
#include <scorum/chain/services/betting_property.hpp>
#include <scorum/chain/dba/db_accessor.hpp>
#include <boost/lambda/lambda.hpp>

namespace scorum {
namespace chain {

dbs_game::dbs_game(database& db)
    : base_service_type(db)
    , _dprops_service(db.dynamic_global_property_service())
    , _betting_props_service(db.betting_property_service())
    , _uuid_hist_dba(db.get_dba<game_uuid_history_object>())
{
}

const game_object& dbs_game::create_game(const uuid_type& uuid,
                                         const std::string& json_metadata,
                                         fc::time_point_sec start,
                                         uint32_t auto_resolve_delay_sec,
                                         const game_type& game,
                                         const fc::flat_set<market_type>& markets)
{
    _uuid_hist_dba.create([&](game_uuid_history_object& o) { o.uuid = uuid; });

    return dbs_service_base<game_service_i>::create([&](game_object& obj) {
        obj.uuid = uuid;
#ifndef IS_LOW_MEM
        fc::from_string(obj.json_metadata, json_metadata);
#endif
        obj.start_time = start;
        obj.auto_resolve_time = start + auto_resolve_delay_sec;
        obj.original_start_time = start;
        obj.last_update = _dprops_service.head_block_time();
        obj.game = game;
        obj.status = game_status::created;

        for (const auto& m : markets)
            obj.markets.emplace(m);
    });
}

void dbs_game::finish(const game_object& game, const fc::flat_set<wincase_type>& wincases)
{
    update(game, [&](game_object& g) {
        if (g.status == game_status::started)
            g.bets_resolve_time = _dprops_service.head_block_time() + _betting_props_service.get().resolve_delay_sec;
        g.status = game_status::finished;
        g.last_update = _dprops_service.head_block_time();
        g.auto_resolve_time = fc::time_point_sec::maximum();

        g.results.clear();
        for (const auto& w : wincases)
            g.results.emplace(w);
    });
}

void dbs_game::update_markets(const game_object& game, const fc::flat_set<market_type>& markets)
{
    update(game, [&](game_object& g) {
        g.markets.clear();
        for (const auto& m : markets)
            g.markets.emplace(m);
    });
}

bool dbs_game::is_exists(int64_t game_id) const
{
    return find_by<by_id>(game_id) != nullptr;
}

bool dbs_game::is_exists(const uuid_type& uuid) const
{
    return find_by<by_uuid>(uuid) != nullptr;
}

const game_object& dbs_game::get_game(int64_t game_id) const
{
    return get_by<by_id>(game_id);
}

const game_object& dbs_game::get_game(const uuid_type& uuid) const
{
    return get_by<by_uuid>(uuid);
}

std::vector<dbs_game::object_cref_type> dbs_game::get_games(fc::time_point_sec start) const
{
    return get_range_by<by_start_time>(::boost::multi_index::unbounded,
                                       ::boost::lambda::_1 <= std::make_tuple(start, ALL_IDS));
}

} // namespace scorum
} // namespace chain
