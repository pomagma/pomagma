#include "aggregate.hpp"
#include "carrier.hpp"
#include "binary_relation.hpp"
#include "nullary_function.hpp"
#include "injective_function.hpp"
#include "binary_function.hpp"
#include "symmetric_function.hpp"
#include "scheduler.hpp"

namespace pomagma
{

namespace detail
{

void inject_one (
        BinaryRelation & destin_rel,
        const BinaryRelation & src_rel,
        const Carrier & src_carrier,
        const std::vector<Ob> & src_to_destin)
{
    for (auto iter = src_carrier.iter(); iter.ok(); iter.next()) {
        Ob src_lhs = * iter;
        Ob destin_lhs = src_to_destin[src_lhs];
        for (auto iter = src_rel.iter_lhs(src_lhs); iter.ok(); iter.next()) {
            Ob src_rhs = * iter;
            Ob destin_rhs = src_to_destin[src_rhs];
            destin_rel.insert(destin_lhs, destin_rhs);
        }
    }
}

void inject_one (
        NullaryFunction & destin_fun,
        const NullaryFunction & src_fun,
        const Carrier & src_carrier __attribute__((unused)),
        const std::vector<Ob> & src_to_destin)
{
    if (Ob src_val = src_fun.find()) {
        Ob destin_val = src_to_destin[src_val];
        destin_fun.insert(destin_val);
    }
}

void inject_one (
        InjectiveFunction & destin_fun,
        const InjectiveFunction & src_fun,
        const Carrier & src_carrier __attribute__((unused)),
        const std::vector<Ob> & src_to_destin)
{
    for (auto iter = src_fun.iter(); iter.ok(); iter.next()) {
        Ob src_arg = * iter;
        Ob src_val = src_fun.find(src_arg);
        Ob destin_arg = src_to_destin[src_arg];
        Ob destin_val = src_to_destin[src_val];
        destin_fun.insert(destin_arg, destin_val);
    }
}

template<class Function>
void inject_one (
        Function & destin_fun,
        const Function & src_fun,
        const Carrier & src_carrier,
        const std::vector<Ob> & src_to_destin)
{
    for (auto iter = src_carrier.iter(); iter.ok(); iter.next()) {
        Ob src_lhs = * iter;
        Ob destin_lhs = src_to_destin[src_lhs];
        for (auto iter = src_fun.iter_lhs(src_lhs); iter.ok(); iter.next()) {
            Ob src_rhs = * iter;
            if (Function::is_symmetric() and src_rhs < src_lhs) { continue; }
            Ob src_val = src_fun.find(src_lhs, src_rhs);
            Ob destin_rhs = src_to_destin[src_rhs];
            Ob destin_val = src_to_destin[src_val];
            destin_fun.insert(destin_lhs, destin_rhs, destin_val);
        }
    }
}

template<class T>
void inject_all (
        const std::unordered_map<std::string, T *> & destin_map,
        const std::unordered_map<std::string, T *> & src_map,
        const Carrier & src_carrier,
        const std::vector<Ob> & src_to_destin)
{

    for (auto pair : destin_map) {
        auto & name = pair.first;
        auto & destin = * pair.second;
        auto i = src_map.find(name);
        if (i == src_map.end()) {
            POMAGMA_WARN("missing " << name);
        } else {
            POMAGMA_INFO("aggregating " << name);
            auto & src = * i->second;
            inject_one(destin, src, src_carrier, src_to_destin);
        }
    }
}

} // namespace detail

void aggregate (
        Structure & destin,
        Structure & src,
        bool clear_src)
{
    POMAGMA_INFO("Aggregating structure");

    POMAGMA_ASSERT(& destin != & src, "cannot aggregate structure into self");
    POMAGMA_ASSERT_LE(
            destin.carrier().item_count() + src.carrier().item_count(),
            destin.carrier().item_dim());

    std::vector<Ob> src_to_destin(1 + src.carrier().item_dim(), 0);
    destin.carrier().set_merge_callback(schedule_merge);
    for (auto iter = src.carrier().iter(); iter.ok(); iter.next()) {
        src_to_destin[*iter] = destin.carrier().unsafe_insert();
    }

#define POMAGMA_INJECT_ALL(arity)\
    detail::inject_all(\
            destin.signature().arity(),\
            src.signature().arity(),\
            src.carrier(),\
            src_to_destin)

    POMAGMA_INJECT_ALL(binary_relations);
    POMAGMA_INJECT_ALL(nullary_functions);
    POMAGMA_INJECT_ALL(injective_functions);
    POMAGMA_INJECT_ALL(binary_functions);
    POMAGMA_INJECT_ALL(symmetric_functions);

#undef POMAGMA_INJECT_ALL

    if (clear_src) { src.clear(); }

    process_mergers(destin.signature());
}

} // namespace pomagma
