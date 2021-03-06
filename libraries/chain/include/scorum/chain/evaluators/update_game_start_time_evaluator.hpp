#pragma once
#include <scorum/protocol/scorum_operations.hpp>
#include <scorum/chain/evaluators/evaluator.hpp>

namespace scorum {
namespace chain {

struct account_service_i;
struct dynamic_global_property_service_i;
struct game_service_i;
struct betting_service_i;
struct database_virtual_operations_emmiter_i;

class update_game_start_time_evaluator : public evaluator_impl<data_service_factory_i, update_game_start_time_evaluator>
{
public:
    using operation_type = scorum::protocol::update_game_start_time_operation;

    update_game_start_time_evaluator(data_service_factory_i&,
                                     betting_service_i&,
                                     database_virtual_operations_emmiter_i&);

    void do_apply(const operation_type& op);

private:
    account_service_i& _account_service;
    dynamic_global_property_service_i& _dprops_service;
    betting_service_i& _betting_service;
    game_service_i& _game_service;
    database_virtual_operations_emmiter_i& _virt_op_emitter;
};
}
}
