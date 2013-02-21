
#include "symmetric_function.hpp"
#include <pomagma/util/aligned_alloc.hpp>
#include <cstring>

namespace pomagma
{

static void noop_callback (const SymmetricFunction *, Ob, Ob) {}

SymmetricFunction::SymmetricFunction (
        const Carrier & carrier,
        void (*insert_callback) (const SymmetricFunction *, Ob, Ob))
    : m_lines(carrier),
      m_block_dim((item_dim() + ITEMS_PER_BLOCK) / ITEMS_PER_BLOCK),
      m_blocks(pomagma::alloc_blocks<Block>(
                  unordered_pair_count(m_block_dim))),
      m_Vlr_table(1 + item_dim()),
      m_insert_callback(insert_callback ? insert_callback : noop_callback)
{
    POMAGMA_DEBUG("creating SymmetricFunction with "
            << unordered_pair_count(m_block_dim) << " blocks");

    std::atomic<Ob> * obs = & m_blocks[0][0];
    size_t block_count = unordered_pair_count(m_block_dim);
    construct_blocks(obs, block_count * ITEMS_PER_BLOCK * ITEMS_PER_BLOCK, 0);
}

SymmetricFunction::~SymmetricFunction ()
{
    std::atomic<Ob> * obs = &m_blocks[0][0];
    size_t block_count = unordered_pair_count(m_block_dim);
    destroy_blocks(obs, block_count * ITEMS_PER_BLOCK * ITEMS_PER_BLOCK);
    pomagma::free_blocks(m_blocks);
}

void SymmetricFunction::validate () const
{
    SharedLock lock(m_mutex);

    POMAGMA_INFO("Validating SymmetricFunction");

    m_lines.validate();

    POMAGMA_DEBUG("validating line-block consistency");
    for (size_t i_ = 0; i_ < m_block_dim; ++i_)
    for (size_t j_ = i_; j_ < m_block_dim; ++j_) {
        const std::atomic<Ob> * block = _block(i_, j_);

        for (size_t _i = 0; _i < ITEMS_PER_BLOCK; ++_i)
        for (size_t _j = 0; _j < ITEMS_PER_BLOCK; ++_j) {
            size_t i = i_ * ITEMS_PER_BLOCK + _i;
            size_t j = j_ * ITEMS_PER_BLOCK + _j;
            if (i == 0 or item_dim() < i) continue;
            if (j < i or item_dim() < j) continue;
            Ob val = _block2value(block, _i, _j);

            if (not (support().contains(i) and support().contains(j))) {
                POMAGMA_ASSERT(not val,
                    "found unsupported value " << i << ',' << j << ',' << val);
            } else if (val) {
                POMAGMA_ASSERT(defined(i, j),
                    "found undefined value " << i << ',' << j << ',' << val);
            } else {
                POMAGMA_ASSERT(not defined(i, j),
                        "found defined null value " << i << ',' << j);
            }
        }
    }

    POMAGMA_DEBUG("validating inverse contains function");
    for (auto lhs_iter = support().iter(); lhs_iter.ok(); lhs_iter.next()) {
        Ob lhs = *lhs_iter;
        for (auto rhs_iter = iter_lhs(lhs); rhs_iter.ok(); rhs_iter.next()) {
            Ob rhs = *rhs_iter;
            Ob val = find(lhs, rhs);

            POMAGMA_ASSERT_CONTAINS3(m_Vlr_table, lhs, rhs, val);
            POMAGMA_ASSERT_CONTAINS3(m_VLr_table, lhs, rhs, val);
        }
    }

    POMAGMA_DEBUG("validating function contains inverse");
    m_Vlr_table.validate(this);
    m_VLr_table.validate(this);
}

void SymmetricFunction::log_stats () const
{
    m_lines.log_stats();
}

size_t SymmetricFunction::count_pairs () const
{
    size_t ordered_pair_count = m_lines.count_pairs();
    size_t diagonal = 0;
    for (auto i = carrier().iter(); i.ok(); i.next()) {
        if (defined(*i, *i)) {
            ++diagonal;
        }
    }
    size_t unordered_pair_count = (ordered_pair_count + diagonal) / 2;
    return unordered_pair_count;
}

void SymmetricFunction::clear ()
{
    memory_barrier();
    m_lines.clear();
    zero_blocks(m_blocks, unordered_pair_count(m_block_dim));
    m_Vlr_table.clear();
    m_VLr_table.clear();
    memory_barrier();
}

void SymmetricFunction::raw_insert (Ob lhs, Ob rhs, Ob val)
{
    POMAGMA_ASSERT5(support().contains(lhs), "unsupported lhs: " << lhs);
    POMAGMA_ASSERT5(support().contains(rhs), "unsupported rhs: " << rhs);
    POMAGMA_ASSERT5(support().contains(val), "unsupported val: " << val);

    value(lhs, rhs).store(val, relaxed);
    m_lines.Lx(lhs, rhs).one(relaxed);
    m_lines.Rx(lhs, rhs).one(relaxed);
    m_Vlr_table.insert(lhs, rhs, val);
    m_Vlr_table.insert(rhs, lhs, val);
    m_VLr_table.insert(lhs, rhs, val);
    m_VLr_table.insert(rhs, lhs, val);
}

void SymmetricFunction::insert (Ob lhs, Ob rhs, Ob val) const
{
    SharedLock lock(m_mutex);

    POMAGMA_ASSERT5(support().contains(lhs), "unsupported lhs: " << lhs);
    POMAGMA_ASSERT5(support().contains(rhs), "unsupported rhs: " << rhs);
    POMAGMA_ASSERT5(support().contains(val), "unsupported val: " << val);

    if (carrier().set_or_merge(value(lhs, rhs), val)) {
        m_lines.Lx(lhs, rhs).one();
        m_lines.Rx(lhs, rhs).one();
        m_Vlr_table.insert(lhs, rhs, val);
        m_Vlr_table.insert(rhs, lhs, val);
        m_VLr_table.insert(lhs, rhs, val);
        m_VLr_table.insert(rhs, lhs, val);
        m_insert_callback(this, lhs, rhs);
    }
}

void SymmetricFunction::unsafe_merge (const Ob dep)
{
    UniqueLock lock(m_mutex);

    POMAGMA_ASSERT5(support().contains(dep), "unsupported dep: " << dep);
    Ob rep = carrier().find(dep);
    POMAGMA_ASSERT5(support().contains(rep), "unsupported rep: " << rep);
    POMAGMA_ASSERT4(rep != dep, "self merge: " << dep << "," << rep);

    // (dep, dep) -> (rep, rep)
    if (defined(dep, dep)) {
        std::atomic<Ob> & dep_val = value(dep, dep);
        std::atomic<Ob> & rep_val = value(rep, rep);
        Ob val = dep_val.load(std::memory_order_relaxed);
        dep_val.store(0, std::memory_order_relaxed);
        m_lines.Lx(dep, dep).zero();
        if (carrier().set_or_merge(rep_val, val)) {
            m_lines.Lx(rep, rep).one();
            m_Vlr_table.unsafe_remove(dep, dep, val).insert(rep, rep, val);
            m_VLr_table.unsafe_remove(dep, dep, val).insert(rep, rep, val);
        } else {
            m_Vlr_table.unsafe_remove(dep, dep, val);
            m_VLr_table.unsafe_remove(dep, dep, val);
        }
    }

    // (dep, rhs) --> (rep, rhs) for rhs != dep
    rep = carrier().find(rep);
    for (auto iter = iter_lhs(dep); iter.ok(); iter.next()) {
        Ob rhs = *iter;
        std::atomic<Ob> & dep_val = value(dep, rhs);
        std::atomic<Ob> & rep_val = value(rep, rhs);
        Ob val = dep_val.load(std::memory_order_relaxed);
        dep_val.store(0, std::memory_order_relaxed);
        m_lines.Rx(dep, rhs).zero();
        if (carrier().set_or_merge(rep_val, val)) {
            m_lines.Rx(rep, rhs).one();
            m_Vlr_table.unsafe_remove(dep, rhs, val).insert(rep, rhs, val);
            m_VLr_table.unsafe_remove(dep, rhs, val).insert(rep, rhs, val);
            m_Vlr_table.unsafe_remove(rhs, dep, val).insert(rhs, rep, val);
            m_VLr_table.unsafe_remove(rhs, dep, val).insert(rhs, rep, val);
        } else {
            m_Vlr_table.unsafe_remove(dep, rhs, val);
            m_VLr_table.unsafe_remove(dep, rhs, val);
            m_Vlr_table.unsafe_remove(rhs, dep, val);
            m_VLr_table.unsafe_remove(rhs, dep, val);
        }
    }
    DenseSet dep_set(item_dim(), m_lines.Lx(dep));
    DenseSet rep_set(item_dim(), m_lines.Lx(rep));
    rep_set.merge(dep_set);

    // dep as val
    rep = carrier().find(rep);
    for (auto iter = iter_val(dep); iter.ok(); iter.next()) {
        Ob lhs = iter.lhs();
        Ob rhs = iter.rhs();
        value(lhs, rhs).store(rep, std::memory_order_relaxed); // XXX error
        // XXX unsupported lhs = 50
        m_Vlr_table.insert(lhs, rhs, rep);
        m_VLr_table.unsafe_remove(lhs, rhs, dep).insert(lhs, rhs, rep);
    }
    m_Vlr_table.unsafe_remove(dep);
}

} // namespace pomagma
