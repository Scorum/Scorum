#pragma once

#include <scorum/chain/evaluators/evaluator.hpp>

namespace scorum {
namespace chain {

class database;

template <typename OperationType, typename DbType = data_service_factory_i> class evaluator_registry
{
public:
    typedef OperationType operation_type;

    evaluator_registry(DbType& d)
        : _db(d)
    {
        for (int i = 0; i < OperationType::count(); i++)
            _op_evaluators.emplace_back();
    }

    template <typename EvaluatorType, typename... Args> void register_evaluator(Args&&... args)
    {
        _op_evaluators[OperationType::template tag<typename EvaluatorType::operation_type>::value].reset(
            new EvaluatorType(_db, std::forward<Args>(args)...));
    }

    template <typename EvaluatorType> void register_evaluator(EvaluatorType* e)
    {
        _op_evaluators[OperationType::template tag<typename EvaluatorType::operation_type>::value].reset(e);
    }

    evaluator<OperationType>& get_evaluator(const OperationType& op)
    {
        int i_which = op.which();
        uint64_t u_which = uint64_t(i_which);
        if (i_which < 0)
            assert("Negative operation tag" && false);
        if (u_which >= _op_evaluators.size())
            assert("No registered evaluator for this operation" && false);
        std::unique_ptr<evaluator<OperationType>>& eval = _op_evaluators[u_which];
        if (!eval)
            assert("No registered evaluator for this operation" && false);
        return *eval;
    }

    std::vector<std::unique_ptr<evaluator<OperationType>>> _op_evaluators;
    DbType& _db;
};

} // namespace chain
} // namespace scorum
