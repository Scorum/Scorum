#include <scorum/chain/services/reward_fund.hpp>
#include <scorum/chain/database/database.hpp>

#include <scorum/chain/schema/scorum_objects.hpp>

namespace scorum {
namespace chain {

dbs_reward_fund::dbs_reward_fund(database& db)
    : dbs_base(db)
{
}

bool dbs_reward_fund::is_exists() const
{
    return nullptr != db_impl().find<reward_fund_object>();
}

const reward_fund_object& dbs_reward_fund::get() const
{
    return db_impl().get<reward_fund_object>();
}

const reward_fund_object& dbs_reward_fund::create(const modifier_type& modifier)
{
    return db_impl().create<reward_fund_object>([&](reward_fund_object& o) { modifier(o); });
}

void dbs_reward_fund::update(const modifier_type& modifier)
{
    db_impl().modify(get(), [&](reward_fund_object& o) { modifier(o); });
}

} // namespace scorum
} // namespace chain
