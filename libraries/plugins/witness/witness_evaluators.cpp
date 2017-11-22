#include <scorum/witness/witness_operations.hpp>
#include <scorum/witness/witness_objects.hpp>

#include <scorum/chain/comment_object.hpp>

namespace scorum {
namespace witness {

void enable_content_editing_evaluator::do_apply(const enable_content_editing_operation& o)
{
    try
    {
        auto edit_lock = _db._temporary_public_impl().find<content_edit_lock_object, by_account>(o.account);

        if (edit_lock == nullptr)
        {
            _db._temporary_public_impl().create<content_edit_lock_object>([&](content_edit_lock_object& lock) {
                lock.account = o.account;
                lock.lock_time = o.relock_time;
            });
        }
        else
        {
            _db._temporary_public_impl().modify(
                *edit_lock, [&](content_edit_lock_object& lock) { lock.lock_time = o.relock_time; });
        }
    }
    FC_CAPTURE_AND_RETHROW((o))
}
}
} // scorum::witness
