#pragma once

#include <scorum/chain/dbs_base_impl.hpp>
#include <vector>
#include <set>
#include <functional>

#include <scorum/chain/registration_objects.hpp>
#include <scorum/chain/account_object.hpp>

namespace scorum {
namespace chain {

struct committee_service_i
{
    virtual bool member_exists(const account_name_type&) const = 0;
};

/** DB service for operations with registration_committee_* objects
 *  --------------------------------------------
 */
class dbs_registration_committee : public committee_service_i, public dbs_base
{
    friend class dbservice_dbs_factory;

protected:
    explicit dbs_registration_committee(database& db);

public:
    using registration_committee_member_refs_type = std::vector<registration_committee_member_object::cref_type>;

    registration_committee_member_refs_type get_committee() const;

    const registration_committee_member_object& get_member(const account_name_type&) const;

    registration_committee_member_refs_type create_committee(const std::vector<account_name_type>& accounts);

    const registration_committee_member_object& add_member(const account_name_type&);

    void exclude_member(const account_name_type&);

    using member_info_modifier_type = std::function<void(registration_committee_member_object&)>;
    void update_member_info(const registration_committee_member_object&, member_info_modifier_type modifier);

    bool member_exists(const account_name_type&) const;

    uint64_t quorum_votes(uint64_t quorum_percent);

private:
    uint64_t _get_members_count() const;

    const registration_committee_member_object& _add_member(const account_object&);

    void _exclude_member(const account_object&);
};

namespace utils {
uint64_t get_quorum(size_t members_count, uint64_t percent);
} // namespace utils

} // namespace chain
} // namespace scorum
