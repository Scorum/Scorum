#pragma once

#include <scorum/chain/database/block_tasks/block_tasks.hpp>

namespace scorum {
namespace protocol {
struct asset;
}
namespace chain {
namespace database_ns {

class process_funds : public block_task
{
public:
    virtual void on_apply(block_task_context&);

private:
    void distribute_reward(block_task_context& ctx, const protocol::asset& reward);
};
}
}
}
