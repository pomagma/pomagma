#pragma once

#include "util.hpp"
#include <vector>
#include <utility>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>

namespace pomagma
{

namespace detail
{

static const size_t HASH_MULTIPLIER = 11400714819323198485ULL;

template<class smallint_t>
struct small_pair_hasher;

template<>
struct small_pair_hasher<uint16_t>
{
    size_t operator() (const std::pair<uint16_t, uint16_t> & pair) const
    {
        static_assert(sizeof(size_t) == 8, "invalid sizeof(size_t)");
        size_t x = pair.first;
        size_t y = pair.second;
        return ((x << 16) | y) * HASH_MULTIPLIER;
    }
};

template<>
struct small_pair_hasher<uint32_t>
{
    size_t operator() (const std::pair<uint32_t, uint32_t> & pair) const
    {
        static_assert(sizeof(size_t) == 8, "invalid sizeof(size_t)");
        size_t x = pair.first;
        size_t y = pair.second;
        return ((x << 32) | y) * HASH_MULTIPLIER;
    }
};

} // namespace detail

// val -> lhs, rhs
class Vlr_Table : noncopyable
{
    typedef tbb::concurrent_unordered_set<
            std::pair<Ob, Ob>,
            detail::small_pair_hasher<Ob>>
        Set;
    typedef std::vector<Set> Data;
    mutable Data m_data;

public:

    Vlr_Table (size_t size) : m_data(size)
    {
    }

    bool contains (Ob lhs, Ob rhs, Ob val) const
    {
        const auto & lr_set = m_data[val];
        return lr_set.find(std::make_pair(lhs, rhs)) != lr_set.end();
    }
    void insert (Ob lhs, Ob rhs, Ob val) const
    {
        m_data[val].insert(std::make_pair(lhs, rhs));
    }

    void clear ()
    {
        for (auto & set : m_data) {
            set.clear();
        }
    }
    Vlr_Table & unsafe_remove (Ob lhs, Ob rhs, Ob val)
    {
        m_data[val].unsafe_erase(std::make_pair(lhs, rhs));
        return * this;
    }
    Vlr_Table & unsafe_remove (Ob val)
    {
        m_data[val].clear();
        return * this;
    }

    class Iterator
    {
        friend class Vlr_Table;

        Set::const_iterator m_iter;
        Set::const_iterator m_end;

        Iterator (const Set & set) : m_iter(set.begin()), m_end(set.end()) {}

    public:

        bool ok () const { return m_iter != m_end; }
        void next () { ++m_iter; }

        Ob lhs () const { POMAGMA_ASSERT_OK return m_iter->first; }
        Ob rhs () const { POMAGMA_ASSERT_OK return m_iter->second; }
    };

    Iterator iter (Ob val) const { return Iterator(m_data[val]); }

    template<class Fun>
    void validate (const Fun * fun) const
    {
        for (Ob val = 1, end = m_data.size(); val < end; ++val) {
            for (auto lhs_rhs : m_data[val]) {
                Ob lhs = lhs_rhs.first;
                Ob rhs = lhs_rhs.second;
                POMAGMA_ASSERT(fun->defined(lhs, rhs),
                    "unsupported keys: " << lhs << "," << rhs);
                POMAGMA_ASSERT_EQ(fun->find(lhs, rhs), val);
            }
        }
    }
};

// val, lhs -> rhs
template<bool transpose>
class VXx_Table : noncopyable
{
    typedef tbb::concurrent_unordered_set<Ob> Set;
    typedef tbb::concurrent_unordered_map<
            std::pair<Ob, Ob>,
            Set,
            detail::small_pair_hasher<Ob>>
        Data;
    mutable Data m_data;

public:

    bool contains (Ob lhs, Ob rhs, Ob val) const
    {
        Ob fixed = transpose ? rhs : lhs;
        Ob moving = transpose ? lhs : rhs;
        Data::const_iterator i = m_data.find(std::make_pair(val, fixed));
        if (i == m_data.end()) {
            return false;
        }
        return i->second.find(moving) != i->second.end();
    }
    void insert (Ob lhs, Ob rhs, Ob val) const
    {
        Ob fixed = transpose ? rhs : lhs;
        Ob moving = transpose ? lhs : rhs;
        m_data[std::make_pair(val, fixed)].insert(moving);
    }

    void clear ()
    {
        m_data.clear();
    }
    VXx_Table<transpose> & unsafe_remove (Ob lhs, Ob rhs, Ob val)
    {
        Ob fixed = transpose ? rhs : lhs;
        Ob moving = transpose ? lhs : rhs;
        Data::const_iterator i = m_data.find(std::make_pair(val, fixed));
        POMAGMA_ASSERT1(i != m_data.end(),
                "double erase: " << val << "," << lhs << "," << rhs);
        i->second.unsafe_erase(moving);
        if (i->second.empty()) {
            m_data.unsafe_erase(i);
        }
        return * this;
    }

    class Iterator
    {
        friend class VXx_Table<transpose>;

        Set::const_iterator m_iter;
        Set::const_iterator m_end;

        Iterator (const Data & data, Ob val, Ob fixed)
        {
            Data::const_iterator i = data.find(std::make_pair(val, fixed));
            if (i != data.end()) {
                m_iter = i->second.begin();
                m_end = i->second.end();
            } else {
                m_iter = m_end;
                POMAGMA_ASSERT6(not (m_iter != m_end),
                    "default constructed iterators do not equality compare");
            }
        }

    public:

        bool ok () const { return m_iter != m_end; }
        void next () { ++m_iter; }

        Ob operator * () const { POMAGMA_ASSERT_OK return *m_iter; }
    };

    Iterator iter (Ob val, Ob fixed) const
    {
        return Iterator(m_data, val, fixed);
    }

    template<class Fun>
    void validate (const Fun * fun) const
    {
        for (const auto & val_fixed : m_data) {
            Ob val = val_fixed.first.first;
            Ob fixed = val_fixed.first.second;
            for (Ob moving : val_fixed.second) {
                Ob lhs = transpose ? moving : fixed;
                Ob rhs = transpose ? fixed : moving;
                POMAGMA_ASSERT(fun->defined(lhs, rhs),
                    "unsupported keys: " << lhs << "," << rhs);
                POMAGMA_ASSERT_EQ(fun->find(lhs, rhs), val);
            }
        }
    }
};

typedef VXx_Table<0> VLr_Table;
typedef VXx_Table<1> VRl_Table;

} // namespace pomagma
