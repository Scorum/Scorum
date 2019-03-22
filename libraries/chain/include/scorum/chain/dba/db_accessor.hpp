#pragma once

#include <chainbase/database_index.hpp>
#include <chainbase/segment_manager.hpp>

#include <scorum/chain/dba/dba.hpp>
#include <scorum/chain/dba/db_accessor_helpers.hpp>
#include <scorum/chain/dba/db_accessor_traits.hpp>
#include <scorum/utils/function_view.hpp>
#include <scorum/utils/any_range.hpp>
#include <scorum/utils/algorithm/foreach_mut.hpp>

namespace scorum {
namespace chain {

struct by_id;

namespace dba {
template <typename TObject> using modifier_type = utils::function_view<void(TObject&)>;
template <typename TObject> using predicate_type = utils::function_view<bool(const TObject&)>;
template <typename TObject> using cref_type = std::reference_wrapper<const TObject>;

namespace detail {

template <typename TObject> size_t size(db_index& db_idx)
{
    return db_idx.get_index<typename chainbase::get_index_type<TObject>::type, by_id>().size();
}

template <typename TObject> const TObject& create(db_index& db_idx, modifier_type<TObject> modifier)
{
    return db_idx.template create<TObject>([&](TObject& o) { modifier(o); });
}

template <typename TObject> const TObject& get_single(db_index& db_idx)
{
    try
    {
        return db_idx.template get<TObject>();
    }
    FC_CAPTURE_AND_RETHROW()
}

template <typename TObject> const TObject& update(db_index& db_idx, const TObject& o, modifier_type<TObject> modifier)
{
    db_idx.modify(o, [&](TObject& o) { modifier(o); });
    return o;
}

template <typename TObject> const TObject& update_single(db_index& db_idx, modifier_type<TObject> modifier)
{
    const auto& o = get_single<TObject>(db_idx);
    db_idx.modify(o, [&](TObject& o) { modifier(o); });
    return o;
}

template <typename TObject> void remove(db_index& db_idx, const TObject& o)
{
    db_idx.remove(o);
}

template <typename TObject> void remove_all(db_index& db_idx, utils::bidir_range<const TObject> items)
{
    utils::foreach_mut(items, [&](const TObject& o) { //
        db_idx.remove(o);
    });
}

template <typename TObject> void remove_single(db_index& db_idx)
{
    db_idx.remove(get_single<TObject>(db_idx));
}

template <typename TObject> bool is_empty(db_index& db_idx)
{
    return nullptr == db_idx.template find<TObject>();
}

template <typename TObject, typename IndexBy, typename Key> const TObject& get_by(db_index& db_idx, const Key& arg)
{
    try
    {
        return db_idx.template get<TObject, IndexBy>(arg);
    }
    FC_CAPTURE_AND_RETHROW()
}

template <typename TObject, typename IndexBy, typename Key> const TObject* find_by(db_index& db_idx, const Key& arg)
{
    try
    {
        return db_idx.template find<TObject, IndexBy>(arg);
    }
    FC_CAPTURE_AND_RETHROW()
}

template <typename TObject, typename IndexBy, typename Key> bool is_exists_by(db_index& db_idx, const Key& arg)
{
    try
    {
        return nullptr != db_idx.template find<TObject, IndexBy>(arg);
    }
    FC_CAPTURE_AND_RETHROW()
}

template <typename TObject, typename IndexBy, typename TKey>
utils::forward_range<const TObject> hashed_get_range_by(db_index& db_idx, const TKey& key)
{
    const auto& idx = db_idx.get_index<typename chainbase::get_index_type<TObject>::type, IndexBy>();
    return idx.equal_range(key);
}

template <typename TObject, typename IndexBy, typename TKey>
utils::bidir_range<const TObject> ordered_get_range_by(db_index& db_idx, const TKey& key)
{
    const auto& idx = db_idx.get_index<typename chainbase::get_index_type<TObject>::type, IndexBy>();
    return idx.equal_range(key);
}

template <typename TIdx, typename TKey> auto get_lower_bound(TIdx& idx, const detail::bound<TKey>& bound);
template <typename TIdx, typename TKey> auto get_upper_bound(TIdx& idx, const detail::bound<TKey>& bound);

template <typename TObject, typename IndexBy, typename TKeyLhs, typename TKeyRhs = TKeyLhs>
utils::bidir_range<const TObject>
get_range_by(db_index& db_idx, const detail::bound<TKeyLhs>& lower, const detail::bound<TKeyRhs>& upper)
{
    const auto& idx = db_idx.get_index<typename chainbase::get_index_type<TObject>::type, IndexBy>();

    auto from = get_lower_bound(idx, lower);
    auto to = get_upper_bound(idx, upper);

    return { from, to };
}

template <typename TObject, typename IndexBy> utils::bidir_range<const TObject> get_all_by(db_index& db_idx)
{
    const auto& idx = db_idx.get_index<typename chainbase::get_index_type<TObject>::type, IndexBy>();

    return { idx.begin(), idx.end() };
}

template <typename TIdx, typename TKey> auto get_lower_bound(TIdx& idx, const detail::bound<TKey>& bound)
{
    switch (bound.kind)
    {
    case bound_kind::unbounded:
        return idx.begin();
    case bound_kind::ge:
    case bound_kind::le:
        return idx.lower_bound(bound.value.get());
    case bound_kind::gt:
    case bound_kind::lt:
        return idx.upper_bound(bound.value.get());
    default:
        FC_ASSERT(false, "Not implemented.");
    }
}

template <typename TIdx, typename TKey> auto get_upper_bound(TIdx& idx, const detail::bound<TKey>& bound)
{
    switch (bound.kind)
    {
    case bound_kind::unbounded:
        return idx.end();
    case bound_kind::ge:
    case bound_kind::le:
        return idx.upper_bound(bound.value.get());
    case bound_kind::gt:
    case bound_kind::lt:
        return idx.lower_bound(bound.value.get());
    default:
        FC_ASSERT(false, "Not implemented.");
    }
}
}

template <typename TObject> class db_accessor
{
public:
    explicit db_accessor(db_index& db_idx)
        : _db_idx(db_idx)
    {
    }

public:
    using object_type = TObject;
    using modifier_type = utils::function_view<void(object_type&)>;
    using predicate_type = utils::function_view<bool(const object_type&)>;
    using object_cref_type = std::reference_wrapper<const TObject>;

    size_t size() const
    {
        return detail::size<TObject>(_db_idx);
    }

    const object_type& create(modifier_type modifier)
    {
        return detail::create(_db_idx, modifier);
    }

    const object_type& update(modifier_type modifier)
    {
        return detail::update_single<TObject>(_db_idx, modifier);
    }

    const object_type& update(const object_type& o, modifier_type modifier)
    {
        return detail::update(_db_idx, o, modifier);
    }

    void remove()
    {
        detail::remove_single<TObject>(_db_idx);
    }

    void remove(const object_type& o)
    {
        detail::remove(_db_idx, o);
    }

    void remove_all(utils::bidir_range<const object_type> items)
    {
        detail::remove_all(_db_idx, items);
    }

    bool is_empty() const
    {
        return detail::is_empty<TObject>(_db_idx);
    }

    const object_type& get() const
    {
        return detail::get_single<TObject>(_db_idx);
    }

    template <class IndexBy, class Key> const object_type& get_by(const Key& arg) const
    {
        return detail::get_by<TObject, IndexBy, Key>(_db_idx, arg);
    }

    template <class IndexBy, class Key> const object_type* find_by(const Key& arg) const
    {
        return detail::find_by<TObject, IndexBy, Key>(_db_idx, arg);
    }

    template <class IndexBy, class Key> bool is_exists_by(const Key& arg) const
    {
        return detail::is_exists_by<TObject, IndexBy, Key>(_db_idx, arg);
    }

    // TODO: using different get_range_by for different indeces (implement using SFINAE):
    //   for ordered indices using get_range_by which returns bidirectional range
    //   for hashed indices using get_range_by which returns forward range
    template <typename IndexBy, typename TKey> utils::bidir_range<const object_type> get_range_by(const TKey& key) const
    {
        return detail::get_range_by<TObject, IndexBy, TKey>(_db_idx, key <= _x, _x <= key);
    }

    template <typename IndexBy, typename TKeyLhs, typename TKeyRhs = TKeyLhs>
    utils::bidir_range<const object_type> get_range_by(const detail::bound<TKeyLhs>& lower,
                                                       const detail::bound<TKeyRhs>& upper) const
    {
        return detail::get_range_by<TObject, IndexBy, TKeyLhs, TKeyRhs>(_db_idx, lower, upper);
    }

    template <typename IndexBy, typename TKey>
    utils::bidir_range<const object_type> get_range_by(unbounded_placeholder lower,
                                                       const detail::bound<TKey>& upper) const
    {
        return detail::get_range_by<TObject, IndexBy, TKey, TKey>(_db_idx, lower, upper);
    }

    template <typename IndexBy, typename TKey>
    utils::bidir_range<const object_type> get_range_by(const detail::bound<TKey>& lower,
                                                       unbounded_placeholder upper) const
    {
        return detail::get_range_by<TObject, IndexBy, TKey, TKey>(_db_idx, lower, upper);
    }

    template <typename IndexBy, typename TKey = index_key_type<TObject, IndexBy>>
    utils::bidir_range<const object_type> get_range_by(unbounded_placeholder lower, unbounded_placeholder upper) const
    {
        return detail::get_range_by<TObject, IndexBy, TKey, TKey>(_db_idx, lower, upper);
    }

    template <typename IndexBy> utils::bidir_range<const object_type> get_all_by() const
    {
        return detail::get_all_by<TObject, IndexBy>(_db_idx);
    }

private:
    db_index& _db_idx;
};
}
}
}
