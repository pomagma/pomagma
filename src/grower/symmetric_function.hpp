#pragma once

#include <pomagma/util/util.hpp>
#include <pomagma/util/dense_set.hpp>
#include "base_bin_rel.hpp"
#include "inverse_bin_fun.hpp"

namespace pomagma
{

inline size_t unordered_pair_count (size_t i) { return (i * (i + 1)) / 2; }

// a tight symmetric binary function tiled in blocks
class SymmetricFunction : noncopyable
{
    mutable base_sym_rel m_lines;
    const size_t m_block_dim;
    Block * const m_blocks;
    Vlr_Table m_Vlr_table;
    VLr_Table m_VLr_table;
    void (*m_insert_callback) (const SymmetricFunction *, Ob, Ob);

    mutable AssertSharedMutex m_mutex;
    typedef AssertSharedMutex::SharedLock SharedLock;
    typedef AssertSharedMutex::UniqueLock UniqueLock;

public:

    SymmetricFunction (
        const Carrier & carrier,
        void (*insert_callback) (const SymmetricFunction *, Ob, Ob) = nullptr);
    ~SymmetricFunction ();
    void validate () const;
    void log_stats () const;

    // raw operations
    // m_blocks is source of truth; m_lines lag
    static bool is_symmetric () { return true; }
    size_t count_pairs () const; // unordered
    void clear ();
    Ob raw_find (Ob lhs, Ob rhs) const { return value(lhs, rhs).load(relaxed); }
    void raw_insert (Ob lhs, Ob rhs, Ob val);

    // relaxed operations
    DenseSet get_Lx_set (Ob lhs) const { return m_lines.Lx_set(lhs); }
    DenseSet get_Rx_set (Ob rhs) const { return m_lines.Rx_set(rhs); }
    bool defined (Ob lhs, Ob rhs) const;
    Ob find (Ob lhs, Ob rhs) const { return value(lhs, rhs).load(acquire); }
    DenseSet::Iterator iter_lhs (Ob lhs) const;
    DenseSet::Iterator iter_rhs (Ob rhs) const;
    Vlr_Table::Iterator iter_val (Ob val) const;
    VLr_Table::Iterator iter_val_lhs (Ob val, Ob lhs) const;
    VLr_Table::Iterator iter_val_rhs (Ob val, Ob lhs) const;
    void insert (Ob lhs, Ob rhs, Ob val) const;

    // strict operations
    void unsafe_merge (const Ob dep);

private:

    const Carrier & carrier () const { return m_lines.carrier(); }
    const DenseSet & support () const { return m_lines.support(); }
    size_t item_dim () const { return m_lines.item_dim(); }

    template<class T>
    static void sort (T & i, T & j) { if (j < i) { T k = j; j = i; i = k; }  }

    std::atomic<Ob> & value (Ob lhs, Ob rhs) const;
    std::atomic<Ob> * _block (int i_, int j_) const
    {
        return m_blocks[unordered_pair_count(j_) + i_];
    }
};

inline bool SymmetricFunction::defined (Ob lhs, Ob rhs) const
{
    POMAGMA_ASSERT5(support().contains(lhs), "unsupported lhs: " << lhs);
    POMAGMA_ASSERT5(support().contains(rhs), "unsupported rhs: " << rhs);
    return m_lines.get_Lx(lhs, rhs);
}

inline std::atomic<Ob> & SymmetricFunction::value (Ob i, Ob j) const
{
    sort(i, j);
    POMAGMA_ASSERT5(support().contains(i), "unsupported lhs: " << i);
    POMAGMA_ASSERT5(support().contains(j), "unsupported rhs: " << j);
    std::atomic<Ob> * block = _block(i / ITEMS_PER_BLOCK, j / ITEMS_PER_BLOCK);
    return _block2value(block, i & BLOCK_POS_MASK, j & BLOCK_POS_MASK);
}

inline DenseSet::Iterator SymmetricFunction::iter_lhs (Ob lhs) const
{
    POMAGMA_ASSERT5(support().contains(lhs), "unsupported lhs: " << lhs);
    return DenseSet::Iterator(item_dim(), m_lines.Lx(lhs));
}

inline DenseSet::Iterator SymmetricFunction::iter_rhs (Ob rhs) const
{
    POMAGMA_ASSERT5(support().contains(rhs), "unsupported rhs: " << rhs);
    return DenseSet::Iterator(item_dim(), m_lines.Rx(rhs));
}

inline Vlr_Table::Iterator SymmetricFunction::iter_val (Ob val) const
{
    POMAGMA_ASSERT5(support().contains(val), "unsupported val: " << val);
    return m_Vlr_table.iter(val);
}

inline VLr_Table::Iterator SymmetricFunction::iter_val_lhs (
        Ob val,
        Ob lhs) const
{
    POMAGMA_ASSERT5(support().contains(val), "unsupported val: " << val);
    POMAGMA_ASSERT5(support().contains(lhs), "unsupported lhs: " << lhs);
    return m_VLr_table.iter(val, lhs);
}

inline VLr_Table::Iterator SymmetricFunction::iter_val_rhs (
        Ob val,
        Ob rhs) const
{
    return iter_val_lhs(val, rhs);
}

} // namespace pomagma
