#pragma once

#include <vector>
#include <set>
#include <functional>

#include <scorum/protocol/proposal_operations.hpp>

#include <scorum/chain/services/dbs_base.hpp>
#include <scorum/chain/schema/dev_committee_object.hpp>

namespace scorum {
namespace chain {

struct development_committee_service_i : public development_committee_i
{
    using member_object_cref_type = std::vector<dev_committee_member_object::cref_type>;

    virtual member_object_cref_type get_committee() const = 0;

    virtual const dev_committee_member_object& get_member(const account_name_type&) const = 0;

    virtual const dev_committee_object& get() const = 0;
};

struct dbs_development_committee : public dbs_base, public development_committee_service_i
{
    void add_member(const account_name_type& account_name) override;
    void exclude_member(const account_name_type& account_name) override;

    void change_add_member_quorum(const percent_type quorum) override;
    void change_exclude_member_quorum(const percent_type quorum) override;
    void change_base_quorum(const percent_type quorum) override;

    percent_type get_add_member_quorum() override;
    percent_type get_exclude_member_quorum() override;
    percent_type get_base_quorum() override;

    bool is_exists(const account_name_type& account_name) const override;

    size_t get_members_count() const override;

    member_object_cref_type get_committee() const override;

    const dev_committee_member_object& get_member(const account_name_type& account) const override;

    const dev_committee_object& get() const override;

private:
    friend class dbservice_dbs_factory;

protected:
    explicit dbs_development_committee(database& db);
};

} // namespace chain
} // namespace scorum
