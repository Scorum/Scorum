#include <scorum/chain/genesis/initializators/advertising_property_initializator.hpp>

#include <scorum/chain/genesis/genesis_state.hpp>
#include <scorum/chain/services/advertising_property_service.hpp>

namespace scorum {
namespace chain {
namespace genesis {

void advertising_property_initializator_impl::on_apply(initializator_context& ctx)
{
    auto& adv_service = ctx.services().advertising_property_service();

    FC_ASSERT(!adv_service.is_exists());

    adv_service.create(
        [&](advertising_property_object& obj) { obj.moderator = ctx.genesis_state().advertising_property.moderator; });
}

} // namespace genesis
} // namespace scorum
} // namespace chain
