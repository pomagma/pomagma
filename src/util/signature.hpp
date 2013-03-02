#pragma once

#include "util.hpp"
#include <unordered_map>

namespace pomagma
{

class Carrier;
class BinaryRelation;
class NullaryFunction;
class InjectiveFunction;
class BinaryFunction;
class SymmetricFunction;

class Signature : noncopyable
{
    Carrier * m_carrier;
    std::unordered_map<std::string, BinaryRelation *> m_binary_relations;
    std::unordered_map<std::string, NullaryFunction *> m_nullary_functions;
    std::unordered_map<std::string, InjectiveFunction *> m_injective_functions;
    std::unordered_map<std::string, BinaryFunction *> m_binary_functions;
    std::unordered_map<std::string, SymmetricFunction *> m_symmetric_functions;

public:

    Signature () : m_carrier(nullptr) {}

    void clear ();

    void declare (Carrier & carrier) { m_carrier = & carrier; }
    void declare (const std::string & name, BinaryRelation & rel);
    void declare (const std::string & name, NullaryFunction & fun);
    void declare (const std::string & name, InjectiveFunction & fun);
    void declare (const std::string & name, BinaryFunction & fun);
    void declare (const std::string & name, SymmetricFunction & fun);

    Carrier * carrier () { return m_carrier; }

    const std::unordered_map<std::string, BinaryRelation *> &
        binary_relations () const;
    const std::unordered_map<std::string, NullaryFunction *> &
        nullary_functions () const;
    const std::unordered_map<std::string, InjectiveFunction *> &
        injective_functions () const;
    const std::unordered_map<std::string, BinaryFunction *> &
        binary_functions () const;
    const std::unordered_map<std::string, SymmetricFunction *> &
        symmetric_functions () const;

    BinaryRelation * binary_relations (const std::string & name) const;
    NullaryFunction * nullary_functions (const std::string & name) const;
    InjectiveFunction * injective_functions (const std::string & name) const;
    BinaryFunction * binary_functions (const std::string & name) const;
    SymmetricFunction * symmetric_functions (const std::string & name) const;

private:

    template<class Function>
    static Function * find (
        const std::unordered_map<std::string, Function *> & funs,
        const std::string & key)
    {
        const auto & i = funs.find(key);
        return i == funs.end() ? nullptr : i->second;
    }
};


inline void Signature::clear ()
{
    POMAGMA_INFO("Clearing signature");
    m_carrier = nullptr;
    m_binary_relations.clear();
    m_nullary_functions.clear();
    m_injective_functions.clear();
    m_binary_functions.clear();
    m_symmetric_functions.clear();
}


inline void Signature::declare (
        const std::string & name,
        BinaryRelation & rel)
{
    m_binary_relations.insert(std::make_pair(name, & rel));
}

inline void Signature::declare (
        const std::string & name,
        NullaryFunction & fun)
{
    m_nullary_functions.insert(std::make_pair(name, & fun));
}

inline void Signature::declare (
        const std::string & name,
        InjectiveFunction & fun)
{
    m_injective_functions.insert(std::make_pair(name, & fun));
}

inline void Signature::declare (
        const std::string & name,
        BinaryFunction & fun)
{
    m_binary_functions.insert(std::make_pair(name, & fun));
}

inline void Signature::declare (
        const std::string & name,
        SymmetricFunction & fun)
{
    m_symmetric_functions.insert(std::make_pair(name, & fun));
}


inline const std::unordered_map<std::string, BinaryRelation *> &
Signature::binary_relations () const
{
    return m_binary_relations;
}

inline const std::unordered_map<std::string, NullaryFunction *> &
Signature::nullary_functions () const
{
    return m_nullary_functions;
}

inline const std::unordered_map<std::string, InjectiveFunction *> &
Signature::injective_functions () const
{
    return m_injective_functions;
}

inline const std::unordered_map<std::string, BinaryFunction *> &
Signature::binary_functions () const
{
    return m_binary_functions;
}

inline const std::unordered_map<std::string, SymmetricFunction *> &
Signature::symmetric_functions () const
{
    return m_symmetric_functions;
}


inline BinaryRelation * Signature::binary_relations (
	const std::string & name) const
{
    return find(m_binary_relations, name);
}

inline NullaryFunction * Signature::nullary_functions (
	const std::string & name) const
{
    return find(m_nullary_functions, name);
}

inline InjectiveFunction * Signature::injective_functions (
	const std::string & name) const
{
    return find(m_injective_functions, name);
}

inline BinaryFunction * Signature::binary_functions (
	const std::string & name) const
{
    return find(m_binary_functions, name);
}

inline SymmetricFunction * Signature::symmetric_functions (
	const std::string & name) const
{
    return find(m_symmetric_functions, name);
}

} // namespace pomagma
