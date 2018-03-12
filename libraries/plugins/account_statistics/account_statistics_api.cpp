#include <scorum/account_statistics/account_statistics_api.hpp>
#include <scorum/account_statistics/account_statistics_plugin.hpp>
#include <scorum/common_statistics/base_api_impl.hpp>

namespace scorum {
namespace account_statistics {

namespace detail {
class account_statistics_api_impl : public common_statistics::common_statistics_api_impl<bucket_object, statistics>
{
public:
    account_statistics_api_impl(scorum::app::application& app)
        : base_api_impl(app, ACCOUNT_STATISTICS_PLUGIN_NAME)
    {
    }
};
} // namespace detail

account_statistics_api::account_statistics_api(const scorum::app::api_context& ctx)
{
    my = std::make_shared<detail::account_statistics_api_impl>(ctx.app);
}

void account_statistics_api::on_api_startup()
{
}

statistics account_statistics_api::get_stats_for_time(const fc::time_point_sec& open, uint32_t interval) const
{
    return my->_app.chain_database()->with_read_lock([&]() { return my->get_stats_for_time(open, interval); });
}

statistics account_statistics_api::get_stats_for_interval(const fc::time_point_sec& start,
                                                          const fc::time_point_sec& end) const
{
    return my->_app.chain_database()->with_read_lock(
        [&]() { return my->get_stats_for_interval<account_statistics_plugin>(start, end); });
}

statistics account_statistics_api::get_lifetime_stats() const
{
    return my->_app.chain_database()->with_read_lock([&]() { return my->get_lifetime_stats(); });
}

statistics account_statistics_api::get_stats_for_time_by_account_name(const account_name_type& account_name,
                                                                      const fc::time_point_sec& open,
                                                                      uint32_t interval) const
{
    return my->_app.chain_database()->with_read_lock([&]() {
        statistics account_stat;
        account_stat.statistic_map[account_name] = my->get_stats_for_time(open, interval).statistic_map[account_name];
        return account_stat;
    });
}

statistics account_statistics_api::get_stats_for_interval_by_account_name(const account_name_type& account_name,
                                                                          const fc::time_point_sec& start,
                                                                          const fc::time_point_sec& end) const
{
    return my->_app.chain_database()->with_read_lock([&]() {
        statistics account_stat;
        account_stat.statistic_map[account_name]
            = my->get_stats_for_interval<account_statistics_plugin>(start, end).statistic_map[account_name];
        return account_stat;
    });
}

statistics account_statistics_api::get_lifetime_stats_by_account_name(const account_name_type& account_name) const
{
    return my->_app.chain_database()->with_read_lock([&]() {
        statistics account_stat;
        account_stat.statistic_map[account_name] = my->get_lifetime_stats().statistic_map[account_name];
        return account_stat;
    });
}

} // namespace account_statistics
} // namespace scorum
