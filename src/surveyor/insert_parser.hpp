#pragma once

#include <pomagma/platform/parser.hpp>
#include <pomagma/microstructure/structure_impl.hpp>

namespace pomagma
{

class InsertReducer
{
public:

    typedef Ob Term;

    InsertReducer(Carrier & carrier) : m_carrier(carrier) {}

    Ob reduce (
            const std::string &,
            const NullaryFunction * fun)
    {
        Ob val = fun->find();
        if (not val) {
            val = m_carrier.try_insert();
            POMAGMA_ASSERT(val, "carrier is full");
            fun->insert(val);
        }
        return val;
    }

    Ob reduce (
            const std::string &,
            const InjectiveFunction * fun,
            Ob key)
    {
        Ob val = fun->find(key);
        if (not val) {
            val = m_carrier.try_insert();
            POMAGMA_ASSERT(val, "carrier is full");
            fun->insert(key, val);
        }
        return val;
    }

    Ob reduce (
            const std::string &,
            const BinaryFunction * fun,
            Ob lhs,
            Ob rhs)
    {
        Ob val = fun->find(lhs, rhs);
        if (not val) {
            val = m_carrier.try_insert();
            POMAGMA_ASSERT(val, "carrier is full");
            fun->insert(lhs, rhs, val);
        }
        return val;
    }

    Ob reduce (
            const std::string &,
            const SymmetricFunction * fun,
            Ob lhs,
            Ob rhs)
    {
        Ob val = fun->find(lhs, rhs);
        if (not val) {
            val = m_carrier.try_insert();
            POMAGMA_ASSERT(val, "carrier is full");
            fun->insert(lhs, rhs, val);
        }
        return val;
    }

private:

    Carrier & m_carrier;
};

class InsertParser : public Parser<InsertReducer>
{
public:

    InsertParser (Signature & signature)
        : Parser<InsertReducer>(signature, m_reducer),
          m_reducer(* signature.carrier())
    {
    }

private:

    InsertReducer m_reducer;
};

} // namespace pomagma
