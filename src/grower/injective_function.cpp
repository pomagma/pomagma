#include "injective_function.hpp"
#include "aligned_alloc.hpp"
#include <cstring>

namespace pomagma
{

static void noop_callback (const InjectiveFunction *, Ob) {}

InjectiveFunction::InjectiveFunction (
        const Carrier & carrier,
        void (*insert_callback) (const InjectiveFunction *, Ob))
    : m_carrier(carrier),
      m_set(support().item_dim()),
      m_inverse_set(support().item_dim()),
      m_values(alloc_blocks<std::atomic<Ob>>(1 + item_dim())),
      m_inverse(alloc_blocks<std::atomic<Ob>>(1 + item_dim())),
      m_insert_callback(insert_callback ? insert_callback : noop_callback)
{
    POMAGMA_DEBUG("creating InjectiveFunction with "
            << item_dim() << " values");

    construct_blocks(m_values, 1 + item_dim(), 0);
    construct_blocks(m_inverse, 1 + item_dim(), 0);
}

InjectiveFunction::~InjectiveFunction ()
{
    destroy_blocks(m_values, 1 + item_dim());
    free_blocks(m_values);
    destroy_blocks(m_inverse, 1 + item_dim());
    free_blocks(m_inverse);
}

void InjectiveFunction::copy_from (const InjectiveFunction & other)
{
    POMAGMA_DEBUG("Copying InjectiveFunction");

    size_t min_item_dim = min(item_dim(), other.item_dim());
    size_t byte_count = sizeof(Ob) * min_item_dim;
    memcpy(m_values, other.m_values, byte_count);
    memcpy(m_inverse, other.m_inverse, byte_count);

    m_set.copy_from(other.m_set);
    m_inverse_set.copy_from(other.m_inverse_set);
}

void InjectiveFunction::validate () const
{
    SharedLock lock(m_mutex);

    POMAGMA_DEBUG("Validating InjectiveFunction");

    m_set.validate();
    m_inverse_set.validate();

    POMAGMA_DEBUG("validating set-values consistency");
    for (Ob key = 1; key <= item_dim(); ++key) {
        bool bit = m_set.contains(key);
        Ob val = m_values[key];

        if (not support().contains(key)) {
            POMAGMA_ASSERT(not val, "found unsupported val at " << key);
            POMAGMA_ASSERT(not bit, "found unsupported bit at " << key);
        } else if (not val) {
            POMAGMA_ASSERT(not bit, "found supported null value at " << key);
        } else {
            POMAGMA_ASSERT(bit, "found unsupported value at " << key);
            POMAGMA_ASSERT(m_carrier.equal(m_inverse[val], key),
                    "value, inverse mismatch: " <<
                    key << " -> " << val << " <- " << m_inverse[val]);
        }
    }

    for (Ob val = 1; val <= item_dim(); ++val) {
        bool bit = m_inverse_set.contains(val);
        Ob key = m_inverse[val];

        if (not support().contains(val)) {
            POMAGMA_ASSERT(not key, "found unsupported key at " << val);
            POMAGMA_ASSERT(not bit, "found unsupported bit at " << val);
        } else if (not key) {
            POMAGMA_ASSERT(not bit, "found supported null key at " << val);
        } else {
            POMAGMA_ASSERT(bit, "found unsupported value at " << val);
            POMAGMA_ASSERT(m_carrier.equal(m_values[key], val),
                    "inverse, value mismatch: " <<
                    val << " <- " << key << " -> " << m_values[key]);
        }
    }
}

inline bool replace (Ob patt, Ob repl, std::atomic<Ob> & destin)
{
    return destin.compare_exchange_strong(
            patt,
            repl,
            std::memory_order_relaxed,
            std::memory_order_relaxed);
}

inline bool set_if_match (Ob ob, std::atomic<Ob> & destin)
{
    return destin.compare_exchange_strong(
            ob,
            0,
            std::memory_order_relaxed,
            std::memory_order_relaxed);
}

void InjectiveFunction::unsafe_merge (Ob dep)
{
    UniqueLock lock(m_mutex);

    POMAGMA_ASSERT_RANGE_(4, dep, item_dim());
    Ob rep = m_carrier.find(dep);
    POMAGMA_ASSERT_RANGE_(4, rep, item_dim());
    POMAGMA_ASSERT4(rep != dep, "self merge: " << dep << "," << rep);

    if (m_set(dep).fetch_zero()) {
        m_set(rep).one();

        std::atomic<Ob> & dep_val = m_values[dep];
        std::atomic<Ob> & rep_val = m_values[rep];
        m_carrier.set_and_merge(rep_val, dep_val.load()); // XXX is this safe?
        dep_val.store(0);
    }
    for (auto iter = this->iter(); iter.ok(); iter.next()) {
        replace(dep, rep, m_values[*iter]);
    }

    rep = m_carrier.find(rep);
    if (m_inverse_set(dep).fetch_zero()) {
        m_inverse_set(rep).one();

        std::atomic<Ob> & dep_val = m_inverse[dep];
        std::atomic<Ob> & rep_val = m_inverse[rep];
        m_carrier.set_and_merge(rep_val, dep_val.load()); // XXX is this safe?
        dep_val.store(0);
    }
    for (auto iter = inverse_iter(); iter.ok(); iter.next()) {
        replace(dep, rep, m_inverse[*iter]);
    }
}

} // namespace pomagma