#pragma once

#include <scorum/protocol/scorum_operations.hpp>

#include <scorum/chain/evaluator.hpp>
#include <scorum/chain/dbservice.hpp>

#include <scorum/chain/dbs_account.hpp>
#include <scorum/chain/dbs_proposal.hpp>
#include <scorum/chain/dbs_registration_committee.hpp>
#include <scorum/chain/dbs_dynamic_global_property.hpp>
#include <scorum/chain/data_service_factory.hpp>

#include <scorum/chain/global_property_object.hpp>

namespace scorum {
namespace chain {

namespace sp = scorum::protocol;

class proposal_create_evaluator_new : public evaluator<scorum::protocol::operation>
{
public:
    using operation_type = scorum::protocol::proposal_create_operation;

    proposal_create_evaluator_new(scorum::chain::data_service_factory_i& services)
        : _services(services)
        , account_service(_services.account_service())
        , proposal_service(_services.proposal_service())
        , committee_service(_services.committee_service())
        , property_service(_services.property_service())
    {
    }

    virtual void apply(const scorum::protocol::operation& o) final override
    {
        const auto& op = o.get<operation_type>();

        this->do_apply(op);
    }

    virtual int get_type() const override
    {
        return scorum::protocol::operation::tag<operation_type>::value;
    }

    void do_apply(const operation_type& op)
    {
        FC_ASSERT((op.lifetime_sec <= SCORUM_PROPOSAL_LIFETIME_MAX_SECONDS
                   && op.lifetime_sec >= SCORUM_PROPOSAL_LIFETIME_MIN_SECONDS),
                  "Proposal life time is not in range of ${min} - ${max} seconds.",
                  ("min", SCORUM_PROPOSAL_LIFETIME_MIN_SECONDS)("max", SCORUM_PROPOSAL_LIFETIME_MAX_SECONDS));

        FC_ASSERT(committee_service.member_exists(op.creator), "Account \"${account_name}\" is not in committee.",
                  ("account_name", op.creator));

        account_service.check_account_existence(op.creator);

        fc::time_point_sec expiration = property_service.head_block_time() + op.lifetime_sec;

        proposal_service.create(op.creator, op.data, *op.action, expiration, get_quorum(*op.action));
    }

    uint64_t get_quorum(sp::proposal_action action)
    {
        const dynamic_global_property_object& properties = property_service.get_dynamic_global_properties();

        switch (action)
        {
        case sp::proposal_action::invite:
            return properties.invite_quorum;

        case sp::proposal_action::dropout:
            return properties.dropout_quorum;

        case sp::proposal_action::change_invite_quorum:
        case sp::proposal_action::change_dropout_quorum:
        case sp::proposal_action::change_quorum:
            return properties.change_quorum;

        default:
            FC_ASSERT("invalid action type.");
        }

        return SCORUM_COMMITTEE_QUORUM_PERCENT;
    }

private:
    scorum::chain::data_service_factory_i& _services;

    scorum::chain::account_service_i& account_service;
    scorum::chain::proposal_service_i& proposal_service;
    scorum::chain::committee_service_i& committee_service;
    scorum::chain::property_service_i& property_service;
};

} // namespace chain
} // namespace scorum
