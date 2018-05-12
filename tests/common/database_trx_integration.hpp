#pragma once

#include "database_integration.hpp"
#include "actoractions.hpp"

#include <scorum/chain/data_service_factory.hpp>

namespace database_fixture {

class database_trx_integration_fixture : public database_integration_fixture
{
public:
    database_trx_integration_fixture();
    virtual ~database_trx_integration_fixture();

    ActorActions actor(const Actor& a);

    share_type get_account_creation_fee() const;

    const account_object& account_create(const std::string& name,
                                         const std::string& creator,
                                         const private_key_type& creator_key,
                                         share_type fee,
                                         const public_key_type& key,
                                         const public_key_type& post_key,
                                         const std::string& json_metadata);

    const account_object&
    account_create(const std::string& name, const public_key_type& key, const public_key_type& post_key);

    const account_object& account_create(const std::string& name, const public_key_type& key);

    const witness_object& witness_create(const std::string& owner,
                                         const private_key_type& owner_key,
                                         const std::string& url,
                                         const public_key_type& signing_key,
                                         const share_type& fee);

    void fund(const std::string& account_name, const share_type& amount);
    void fund(const std::string& account_name, const asset& amount);
    void transfer(const std::string& from, const std::string& to, const asset& amount);
    void transfer_to_scorumpower(const std::string& from, const std::string& to, const asset& amount);
    void transfer_to_scorumpower(const std::string& from, const std::string& to, const share_type& amount);
    void vest(const std::string& account, const asset& amount);
    void vest(const std::string& from, const share_type& amount);
    void proxy(const std::string& account, const std::string& proxy);
    const asset& get_balance(const std::string& account_name) const;
    void sign(signed_transaction& trx, const fc::ecc::private_key& key);

    template <typename T>
    void push_operation_only(const T& op, const fc::ecc::private_key& key = fc::ecc::private_key())
    {
        push_operation(op, key, false);
    }

protected:
    virtual void open_database_impl(const genesis_state_type& genesis);

    signed_transaction trx;

    scorum::chain::data_service_factory_i& services;
};

} // database_fixture
