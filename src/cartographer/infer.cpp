#include "infer.hpp"
#include <pomagma/macrostructure/structure_impl.hpp>
#include <pomagma/macrostructure/scheduler.hpp>
#include <mutex>

#define POMAGMA_ASSERT_UNDECIDED(rel, x, y)\
    POMAGMA_ASSERT(not rel.find(x, y),\
        "already decided " #rel " " << x << " " << y)

namespace pomagma
{

namespace
{

class TheoremQueue
{
    BinaryRelation & m_rel;
    std::vector<std::pair<Ob, Ob>> m_queue;

public:

    TheoremQueue (BinaryRelation & rel) : m_rel(rel) {}
    ~TheoremQueue ()
    {
        POMAGMA_ASSERT(m_queue.empty(), "theorems have not been flushed");
    }

    void push (Ob x, Ob y)
    {
        m_queue.push_back(std::make_pair(x, y));
    }

    void try_push (Ob x, Ob y)
    {
        if (unlikely(not m_rel.find(x, y))) {
            push(x, y);
        }
    }

    void flush (std::mutex & mutex)
    {
        if (not m_queue.empty()) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                for (const auto & pair : m_queue) {
                    m_rel.insert(pair.first, pair.second);
                }
            }
            m_queue.clear();
        }
    }
};

class LhsFixedTheoremQueue
{
    BinaryRelation & m_rel;
    Ob m_lhs;
    DenseSet m_rhs;

public:

    LhsFixedTheoremQueue (BinaryRelation & rel)
        : m_rel(rel),
          m_lhs(0),
          m_rhs(rel.item_dim())
    {
    }

    void push (Ob lhs, Ob rhs)
    {
        POMAGMA_ASSERT1(
            m_lhs == 0 or m_lhs == lhs,
            "mismatched lhs in LhsFixedTheoremQueue; use TheoremQueue instead");
        m_lhs = lhs;
        m_rhs.insert(rhs);
    }

    void flush (std::mutex & mutex)
    {
        if (m_lhs) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                m_rel.insert(m_lhs, m_rhs);
            }
            m_lhs = 0;
            m_rhs.zero();
        }
    }
};

// All the nonconst filtering below is only an optimization.
// Specifically, in rules like
//
//        LESS x y
//   --------------------
//   LESS APP f x APP f y
//
// we skip trivial values f = K z for any z, since K z x = z = K z y.
DenseSet get_nonconst (Structure & structure)
{
    const Carrier & carrier = structure.carrier();
    const NullaryFunction & K = structure.nullary_function("K");
    const BinaryFunction & APP = structure.binary_function("APP");

    DenseSet nonconst(carrier.item_dim());
    nonconst = carrier.support();
    if (Ob K_ = K.find()) {
        for (auto iter = APP.iter_lhs(K_); iter.ok(); iter.next()) {
            Ob x = * iter;
            Ob APP_K_x = APP.find(K_, x);
            nonconst.remove(APP_K_x);
        }
    }

    size_t total_count = carrier.item_count();
    size_t const_count = total_count - nonconst.count_items();
    POMAGMA_INFO(
        "found " << const_count << " / " << total_count << " constant obs");

    return nonconst;
}

// LESS x z   LESS z y
// -------------------
//      LESS x y
void infer_less_transitive (
    const Carrier & carrier,
    BinaryRelation & LESS,
    const BinaryRelation & NLESS)
{
    POMAGMA_INFO("Inferring LESS-transitive");

    const size_t item_dim = carrier.item_dim();

    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet y_set(item_dim);
        LhsFixedTheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (Ob x = 1; x <= item_dim; ++x) {
            if (not carrier.contains(x)) { continue; }
            const DenseSet less_x = LESS.get_Lx_set(x);

            y_set.set_pnn(
                carrier.support(),
                LESS.get_Lx_set(x),
                NLESS.get_Lx_set(x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                POMAGMA_ASSERT(carrier.contains(y), "unsupported ob: " << y);
                POMAGMA_ASSERT_UNDECIDED(NLESS, x, y);
                POMAGMA_ASSERT_UNDECIDED(LESS, x, y);

                if (unlikely(less_x.intersects(LESS.get_Rx_set(y)))) {
                    theorems.push(x, y);
                }
            }

            theorems.flush(mutex);
        }
    }
}

inline void infer_less_monotone_nonconst (
        const BinaryRelation & LESS,
        const BinaryFunction & fun,
        const DenseSet & nonconst,
        const Ob f,
        DenseSet & x_set,
        DenseSet & y_set,
        TheoremQueue & theorems)
{
    for (auto iter = LESS.iter_lhs(f); iter.ok(); iter.next()) {
        Ob g = * iter;
        if (unlikely(g == f)) {

            for (auto iter = fun.iter_lhs(f); iter.ok(); iter.next()) {
                Ob x = * iter;
                Ob fx = fun.find(f, x);
                y_set.set_insn(fun.get_Lx_set(f), LESS.get_Lx_set(x));
                y_set.remove(x);
                for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                    Ob y = * iter;
                    Ob fy = fun.find(f, y);
                    theorems.try_push(fx, fy);
                }
            }

        } else if (nonconst(g)) {

            x_set.set_insn(fun.get_Lx_set(f), fun.get_Lx_set(g));
            for (auto iter = x_set.iter(); iter.ok(); iter.next()) {
                Ob x = * iter;
                Ob fx = fun.find(f, x);
                Ob gx = fun.find(g, x);
                theorems.try_push(fx, gx);
            }

            x_set.set_diff(fun.get_Lx_set(f), fun.get_Lx_set(g));
            for (auto iter = x_set.iter(); iter.ok(); iter.next()) {
                Ob x = * iter;
                Ob fx = fun.find(f, x);
                const DenseSet less_fx = LESS.get_Lx_set(fx);
                y_set.set_ppn(
                    LESS.get_Lx_set(x),
                    fun.get_Lx_set(g),
                    fun.get_Lx_set(f));
                for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                    Ob y = * iter;
                    Ob gy = fun.find(g, y);
                    if (unlikely(not less_fx(gy))) {
                        theorems.push(fx, gy);
                    }
                }
            }

        } else if (Ob g_ = fun.find(g, g)) {

            for (auto iter = fun.iter_lhs(f); iter.ok(); iter.next()) {
                Ob x = * iter;
                Ob fx = fun.find(f, x);
                theorems.try_push(fx, g_);
            }
        }
    }
}

inline void infer_less_monotone_const (
        const BinaryRelation & LESS,
        const BinaryFunction & fun,
        const DenseSet & nonconst,
        const Ob f,
        const Ob f_,
        TheoremQueue & theorems)
{
    for (auto iter = LESS.iter_lhs(f); iter.ok(); iter.next()) {
        Ob g = * iter;
        if (nonconst(g)) {

            for (auto iter = fun.iter_lhs(g); iter.ok(); iter.next()) {
                Ob x = * iter;
                Ob gx = fun.find(g, x);
                theorems.try_push(f_, gx);
            }

        } else if (Ob g_ = fun.find(g, g)) {

            theorems.try_push(f_, g_);
        }
    }
}

//      LESS f g               LESS x y          LESS f g    LESS x y
// --------------------   --------------------   --------------------
// LESS fun f x fun g x   LESS fun f x fun f y   LESS fun f x fun g y
//
// FIXME this implementation is not complete for the above rules
void infer_less_monotone (
        const Carrier & carrier,
        BinaryRelation & LESS,
        const BinaryFunction & fun,
        const DenseSet & nonconst)
{
    POMAGMA_INFO("Inferring binary LESS-monotone");

    const size_t item_dim = carrier.item_dim();

    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet x_set(item_dim);
        DenseSet y_set(item_dim);
        TheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (Ob f = 1; f <= item_dim; ++f) {
            if (not carrier.contains(f)) { continue; }
            if (nonconst(f)) {

                infer_less_monotone_nonconst(
                    LESS,
                    fun,
                    nonconst,
                    f,
                    x_set,
                    y_set,
                    theorems);
                theorems.flush(mutex);

            } else if (Ob f_ = fun.find(f, f)) {

                infer_less_monotone_const(
                    LESS,
                    fun,
                    nonconst,
                    f,
                    f_,
                    theorems);
                theorems.flush(mutex);
            }
        }
    }
}

//        LESS f g           LESS f g    LESS x y
// ----------------------   ----------------------
// LESS RAND f x RAND g x   LESS RAND f x RAND g y
void infer_less_monotone (
    const Carrier & carrier,
    BinaryRelation & LESS,
    const SymmetricFunction & RAND)
{
    POMAGMA_INFO("Inferring symmetric LESS-monotone");

    const size_t item_dim = LESS.item_dim();
    std::vector<Ob> f_set;
    for (auto iter = carrier.iter(); iter.ok(); iter.next()) {
        Ob f = * iter;
        f_set.push_back(f);
    }
    const size_t f_count = f_set.size();

    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet g_set(item_dim);
        DenseSet x_set(item_dim);
        DenseSet y_set(item_dim);
        TheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (size_t iter = 0; iter < f_count; ++iter) {
            Ob f = f_set[iter];
            g_set = LESS.get_Lx_set(f);
            g_set.remove(f);
            for (auto iter = g_set.iter(); iter.ok(); iter.next()) {
                Ob g = * iter;

                x_set.set_insn(RAND.get_Lx_set(f), RAND.get_Lx_set(g));
                for (auto iter = x_set.iter(); iter.ok(); iter.next()) {
                    Ob x = * iter;
                    Ob fx = RAND.find(f, x);
                    Ob gx = RAND.find(g, x);
                    theorems.try_push(fx, gx);
                }

                x_set.set_diff(RAND.get_Lx_set(f), RAND.get_Lx_set(g));
                for (auto iter = x_set.iter(); iter.ok(); iter.next()) {
                    Ob x = * iter;
                    Ob fx = RAND.find(f, x);
                    const DenseSet less_fx = LESS.get_Lx_set(fx);
                    y_set.set_ppn(
                        LESS.get_Lx_set(x),
                        RAND.get_Lx_set(g),
                        RAND.get_Lx_set(f));
                    for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                        Ob y = * iter;
                        Ob gy = RAND.find(g, y);
                        if (unlikely(not less_fx(gy))) {
                            theorems.push(fx, gy);
                        }
                    }
                }
            }

            theorems.flush(mutex);
        }
    }
}

//        LESS f g                 LESS x y           LESS f g    LESS x y
// ----------------------   ----------------------   ----------------------
// LESS JOIN f x JOIN g x   LESS JOIN f x JOIN f y   LESS JOIN f x JOIN g y
void infer_less_join_monotone (
    const Carrier & carrier,
    BinaryRelation & LESS,
    const SymmetricFunction & JOIN)
{
    POMAGMA_INFO("Inferring LESS-JOIN-monotone");

    const size_t item_dim = LESS.item_dim();
    std::vector<Ob> f_set;
    for (auto iter = carrier.iter(); iter.ok(); iter.next()) {
        Ob f = * iter;
        f_set.push_back(f);
    }
    const size_t f_count = f_set.size();

    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet x_set(item_dim);
        DenseSet g_set(item_dim);
        DenseSet y_set(item_dim);
        TheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (size_t iter = 0; iter < f_count; ++iter) {
            Ob f = f_set[iter];
            x_set.set_pnn(
                JOIN.get_Lx_set(f),     // if JOIN f x is defined
                LESS.get_Lx_set(f),     // and JOIN f x != f
                LESS.get_Rx_set(f));    // and JOIN f x != x
            for (auto iter = x_set.iter(); iter.ok(); iter.next()) {
                Ob x = * iter;
                if (unlikely(x >= f)) {
                    break;
                }
                Ob fx = JOIN.find(f, x);
                const DenseSet less_fx = LESS.get_Lx_set(fx);

                theorems.try_push(f, fx);
                theorems.try_push(x, fx);

                g_set.set_insn(
                    LESS.get_Lx_set(f),     // if LESS f g
                    JOIN.get_Lx_set(x));    // and JOIN g x is defined
                for (auto iter = g_set.iter(); iter.ok(); iter.next()) {
                    Ob g = * iter;
                    Ob gx = JOIN.find(g, x);
                    if (unlikely(not less_fx(gx))) {
                        theorems.push(fx, gx);
                    }
                }

                y_set.set_insn(
                    LESS.get_Lx_set(x),     // if LESS x y
                    JOIN.get_Lx_set(f));    // and JOIN f y is defined
                for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                    Ob y = * iter;
                    Ob fy = JOIN.find(f, y);
                    if (unlikely(not less_fx(fy))) {
                        theorems.push(fx, fy);
                    }
                }

                g_set.set_diff(
                    LESS.get_Lx_set(f),     // if LESS f g
                    JOIN.get_Lx_set(x));    // and JOIN g x is not defined
                for (auto iter = g_set.iter(); iter.ok(); iter.next()) {
                    Ob g = * iter;
                    y_set.set_ppnn(
                        LESS.get_Lx_set(x),     // if LESS x y
                        JOIN.get_Lx_set(g),     // and JOIN g y is defined
                        LESS.get_Lx_set(g),     // and JOIN g y != g
                        LESS.get_Rx_set(g));    // and JOIN g y != y
                    for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                        Ob y = * iter;
                        Ob gy = JOIN.find(g, y);
                        if (unlikely(not less_fx(gy))) {
                            theorems.push(fx, gy);
                        }
                    }
                }
            }

            theorems.flush(mutex);
        }
    }
}

// LESS x z   LESS y z
// -------------------
//   LESS JOIN x y z
void infer_less_convex (
    const Carrier & carrier,
    BinaryRelation & LESS,
    const SymmetricFunction & JOIN)
{
    POMAGMA_INFO("Inferring LESS-JOIN-convex");

    const size_t item_dim = carrier.item_dim();
    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet z_set(item_dim);
        DenseSet y_set(item_dim);
        TheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (Ob x = 1; x <= item_dim; ++x) {
            if (not carrier.contains(x)) { continue; }

            y_set.set_pnn(
                JOIN.get_Lx_set(x),
                LESS.get_Lx_set(x),
                LESS.get_Rx_set(x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                if (unlikely(y >= x)) {
                    break;
                }
                Ob xy = JOIN.find(x, y);
                z_set.set_ppn(
                    LESS.get_Lx_set(x),
                    LESS.get_Lx_set(y),
                    LESS.get_Lx_set(xy));
                for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
                    Ob z = * iter;
                    theorems.push(xy, z);
                }
            }

            theorems.flush(mutex);
        }
    }
}

//  LESS x z   LESS y z   LESS z x   LESS z y
//  -------------------   -------------------
//    LESS RAND x y z       LESS z RAND x y
void infer_less_linear (
    const Carrier & carrier,
    BinaryRelation & LESS,
    const SymmetricFunction & RAND)
{
    POMAGMA_INFO("Inferring LESS-RAND-linear");

    const size_t item_dim = carrier.item_dim();
    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet z_set(item_dim);
        DenseSet y_set(item_dim);
        TheoremQueue theorems(LESS);

        #pragma omp for schedule(dynamic, 1)
        for (Ob x = 1; x <= item_dim; ++x) {
            if (not carrier.contains(x)) { continue; }

            y_set.set_pnn(
                RAND.get_Lx_set(x),
                LESS.get_Lx_set(x),
                LESS.get_Rx_set(x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                if (y >= x) {
                    break;
                }
                Ob xy = RAND.find(x, y);

                z_set.set_ppn(
                    LESS.get_Lx_set(x),
                    LESS.get_Lx_set(y),
                    LESS.get_Lx_set(xy));
                for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
                    Ob z = * iter;
                    theorems.push(xy, z);
                }

                z_set.set_ppn(
                    LESS.get_Rx_set(x),
                    LESS.get_Rx_set(y),
                    LESS.get_Rx_set(xy));
                for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
                    Ob z = * iter;
                    theorems.push(z, xy);
                }
            }

            theorems.flush(mutex);
        }
    }
}

// NLESS x z   LESS y z   LESS z x   NLESS z y
// --------------------   --------------------
//      NLESS x y               NLESS x y
inline bool infer_nless_transitive (
    const BinaryRelation & LESS,
    const BinaryRelation & NLESS,
    Ob x,
    Ob y)
{
    return NLESS.get_Lx_set(x).intersects(LESS.get_Lx_set(y))
        or LESS.get_Rx_set(x).intersects(NLESS.get_Rx_set(y));
}

// NLESS fun x z fun y z   NLESS fun z x fun z y
// ---------------------   ---------------------
//       NLESS x y               NLESS x y
inline bool infer_nless_monotone (
    const BinaryRelation & NLESS,
    const BinaryFunction & fun,
    const DenseSet & nonconst,
    Ob x,
    Ob y,
    DenseSet & z_set)
{
    if (nonconst.contains(x) or nonconst.contains(y)) {
        z_set.set_insn(fun.get_Lx_set(x), fun.get_Lx_set(y));
        for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
            Ob z = * iter;
            Ob xz = fun.find(x, z);
            Ob yz = fun.find(y, z);
            if (unlikely(NLESS.find(xz, yz))) {
                return true;
            }
        }
    }

    z_set.set_insn(fun.get_Rx_set(x), fun.get_Rx_set(y), nonconst);
    for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
        Ob z = * iter;
        Ob zx = fun.find(z, x);
        Ob zy = fun.find(z, y);
        if (unlikely(NLESS.find(zx, zy))) {
            return true;
        }
    }

    return false;
}

// NLESS fun x z fun y z
// ---------------------
//       NLESS x y
inline bool infer_nless_monotone (
    const BinaryRelation & NLESS,
    const SymmetricFunction & fun,
    Ob x,
    Ob y,
    DenseSet & z_set)
{
    z_set.set_insn(fun.get_Lx_set(x), fun.get_Lx_set(y));
    for (auto iter = z_set.iter(); iter.ok(); iter.next()) {
        Ob z = * iter;
        Ob xz = fun.find(x, z);
        Ob yz = fun.find(y, z);
        if (unlikely(NLESS.find(xz, yz))) {
            return true;
        }
    }

    return false;
}

// TODO infer associativity
//
// ----------------------------------
// EQUAL APP COMP x y z APP x APP y z
//
// -------------------------------------
// EQUAL JOIN JOIN x y z JOIN x JOIN y z
//
// -------------------------------------
// EQUAL RAND RAND x y z RAND x RAND y z

} // anonymous namespace

// ---------------------   ----------------------------
// EQUAL APP APP K x y x   EQUAL COMP APP K x y APP K x
size_t infer_const (Structure & structure)
{
    POMAGMA_INFO("Inferring K");

    const Carrier & carrier = structure.carrier();
    const NullaryFunction & K = structure.nullary_function("K");
    BinaryFunction & APP = structure.binary_function("APP");
    BinaryFunction & COMP = structure.binary_function("COMP");

    DenseSet y_set(carrier.item_dim());

    size_t theorem_count = 0;
    if (Ob K_ = K.find()) {
        for (auto iter = APP.iter_lhs(K_); iter.ok(); iter.next()) {
            Ob x = * iter;
            Ob APP_K_x = APP.find(K_, x);

            y_set.set_diff(carrier.support(), APP.get_Lx_set(APP_K_x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                APP.insert(APP_K_x, y, x);
                ++theorem_count;
            }

            y_set.set_diff(carrier.support(), COMP.get_Lx_set(APP_K_x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                COMP.insert(APP_K_x, y, APP_K_x);
                ++theorem_count;
            }
        }
    }

    POMAGMA_INFO("inferred " << theorem_count << " K facts");
    return theorem_count;
}

size_t infer_nless (Structure & structure)
{
    POMAGMA_INFO("Inferring NLESS");

    Signature & signature = structure.signature();
    const Carrier & carrier = structure.carrier();
    const BinaryRelation & LESS = structure.binary_relation("LESS");
    BinaryRelation & NLESS = structure.binary_relation("NLESS");
    const BinaryFunction & APP = structure.binary_function("APP");
    const BinaryFunction & COMP = structure.binary_function("COMP");
    const SymmetricFunction * JOIN = signature.symmetric_function("JOIN");
    const SymmetricFunction * RAND = signature.symmetric_function("RAND");
    const DenseSet nonconst = get_nonconst(structure);
    const size_t item_dim = carrier.item_dim();

    size_t start_count = NLESS.count_pairs();

    std::mutex mutex;
    #pragma omp parallel
    {
        DenseSet y_set(item_dim);
        DenseSet z_set(item_dim);
        LhsFixedTheoremQueue theorems(NLESS);

        #pragma omp for schedule(dynamic, 1)
        for (Ob x = 1; x <= item_dim; ++x) {
            if (not carrier.contains(x)) { continue; }

            y_set.set_pnn(
                carrier.support(),
                LESS.get_Lx_set(x),
                NLESS.get_Lx_set(x));
            for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
                Ob y = * iter;
                POMAGMA_ASSERT(carrier.contains(y), "unsupported ob: " << y);
                POMAGMA_ASSERT_UNDECIDED(LESS, x, y);
                POMAGMA_ASSERT_UNDECIDED(NLESS, x, y);

                if (infer_nless_transitive(LESS, NLESS, x, y) or
                    infer_nless_monotone(NLESS, APP, nonconst, x, y, z_set) or
                    infer_nless_monotone(NLESS, COMP, nonconst, x, y, z_set) or
                    (JOIN and
                     infer_nless_monotone(NLESS, * JOIN, x, y, z_set)) or
                    (RAND and
                     infer_nless_monotone(NLESS, * RAND, x, y, z_set)))
                {
                    theorems.push(x, y);
                }
            }

            theorems.flush(mutex);
        }
    }

    size_t theorem_count = NLESS.count_pairs() - start_count;
    POMAGMA_INFO("inferred " << theorem_count << " NLESS facts");
    return theorem_count;
}

size_t infer_less (Structure & structure)
{
    POMAGMA_INFO("Inferring LESS");

    Signature & signature = structure.signature();
    const Carrier & carrier = structure.carrier();
    BinaryRelation & LESS = structure.binary_relation("LESS");
    const BinaryRelation & NLESS = structure.binary_relation("NLESS");
    const BinaryFunction & APP = structure.binary_function("APP");
    const BinaryFunction & COMP = structure.binary_function("COMP");
    const SymmetricFunction * JOIN = signature.symmetric_function("JOIN");
    const SymmetricFunction * RAND = signature.symmetric_function("RAND");
    const DenseSet nonconst = get_nonconst(structure);

    size_t start_count = LESS.count_pairs();

    infer_less_transitive(carrier, LESS, NLESS);
    infer_less_monotone(carrier, LESS, APP, nonconst);
    infer_less_monotone(carrier, LESS, COMP, nonconst);
    if (JOIN) {
        infer_less_join_monotone(carrier, LESS, * JOIN);
        infer_less_convex(carrier, LESS, * JOIN);
    }
    if (RAND) {
        infer_less_monotone(carrier, LESS, * RAND);
        infer_less_linear(carrier, LESS, * RAND);
    }

    size_t theorem_count = LESS.count_pairs() - start_count;
    POMAGMA_INFO("inferred " << theorem_count << " LESS facts");
    return theorem_count;
}

size_t infer_equal (Structure & structure)
{
    POMAGMA_INFO("Inferring EQUAL");

    Carrier & carrier = structure.carrier();
    const BinaryRelation & LESS = structure.binary_relation("LESS");

    DenseSet y_set(carrier.item_dim());

    size_t start_item_count = carrier.item_count();
    carrier.set_merge_callback(schedule_merge);

    for (auto iter = carrier.iter(); iter.ok(); iter.next()) {
        Ob x = * iter;

        y_set.set_insn(LESS.get_Lx_set(x), LESS.get_Rx_set(x));
        for (auto iter = y_set.iter(); iter.ok(); iter.next()) {
            Ob y = * iter;
            if (likely(y < x)) {
                carrier.merge(x, y);
            } else {
                break;
            }
        }
    }

    process_mergers(structure.signature());

    size_t theorem_count = start_item_count - carrier.item_count();
    POMAGMA_INFO("inferred " << theorem_count << " EQUAL facts");
    return theorem_count;
}

size_t infer_pos (Structure & structure)
{
    size_t theorem_count = 0;
    theorem_count += infer_const(structure);
    theorem_count += infer_less(structure);
    theorem_count += infer_equal(structure);
    return theorem_count;
}

size_t infer_neg (Structure & structure)
{
    size_t theorem_count = 0;
    theorem_count += infer_nless(structure);
    return theorem_count;
}

} // namespace pomagma
