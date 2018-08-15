#pragma once
#include <scorum/chain/dba/db_accessor.hpp>
#include <scorum/chain/dba/db_accessor_mock.hpp>

namespace scorum {
namespace chain {
namespace dba {

template <typename TObject> struct db_accessor_i
{
    template <typename T>
    db_accessor_i(T&& inst)
        : _inst(std::forward<T>(inst))
    {
    }

    using object_type = TObject;
    using modifier_type = typename std::function<void(object_type&)>;
    using object_cref_type = std::reference_wrapper<const TObject>;

    const object_type& create(const modifier_type& modifier)
    {
        return _inst.visit([&](auto& x) -> decltype(auto) { return x.create(modifier); });
    }

    void update(const modifier_type& modifier)
    {
        _inst.visit([&](auto& x) { x.update(modifier); });
    }

    void update(const object_type& o, const modifier_type& modifier)
    {
        _inst.visit([&](auto& x) { x.update(o, modifier); });
    }

    void remove()
    {
        _inst.visit([&](auto& x) { x.remove(); });
    }

    void remove(const object_type& o)
    {
        _inst.visit([&](auto& x) { x.remove(o); });
    }

    bool is_empty() const
    {
        return _inst.visit([&](auto& x) -> decltype(auto) { return x.is_empty(); });
    }

    const object_type& get() const
    {
        return _inst.visit([&](auto& x) -> decltype(auto) { return x.get(); });
    }

    template <class IndexBy, class Key> const object_type& get_by(const Key& arg) const
    {
        return _inst.visit([&](auto& x) -> decltype(auto) { return x.get_by<IndexBy>(arg); });
    }

    template <class IndexBy, class Key> const object_type* find_by(const Key& arg) const
    {
        return _inst.visit([&](auto& x) -> decltype(auto) { return x.find_by<IndexBy>(arg); });
    }

    template <class IndexBy, class TLower, class TUpper>
    std::vector<object_cref_type> get_range_by(TLower&& lower, TUpper&& upper) const
    {
        return _inst.visit([&](auto& x) -> decltype(auto) {
            return x.get_range_by<IndexBy>(std::forward<TLower>(lower), std::forward<TUpper>(upper));
        });
    }

    template <typename TAccessor> const TAccessor& get_accessor_inst() const
    {
        return _inst.template get<TAccessor>();
    }

    template <typename TAccessor> TAccessor& get_accessor_inst()
    {
        return _inst.template get<TAccessor>();
    }

private:
    fc::static_variant<db_accessor<TObject>, db_accessor_mock<TObject>> _inst;
};
} // dba
} // chain
} // scorum
