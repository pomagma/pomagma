#include "sampler.hpp"
#include "nullary_function.hpp"
#include "injective_function.hpp"
#include "binary_function.hpp"
#include "symmetric_function.hpp"

namespace pomagma
{

Sampler::Sampler (Carrier & carrier)
    : m_carrier(carrier)
{
}

template<class Key>
inline Key sample (
        const std::unordered_map<Key, float> & probs,
        float total)
{
    while (true) {
        float r = random_01() * total;
        for (const auto & pair : probs) {
            Key key = pair.first;
            float prob = pair.second;
            if ((r -= prob) < 0) {
                return key;
            }
        }

        // occasionally fall through due to rounding error
    }
};

void Sampler::unsafe_insert_random ()
{
    // TODO measure rejection rate
    while (true) {
        auto pair = try_insert_random();
        bool inserted = pair.second;
        if (inserted) {
            return;
        }
    }
}

std::pair<Ob, bool> Sampler::try_insert_random ()
{
    while (true) {
        float r = random_01();

        if ((r -= m_nullary_prob) < 0) {
            auto & fun = * sample(m_nullary_probs, m_nullary_prob);
            Ob val = fun.find(); // TODO get_rep
            return std::make_pair(val, false);
        }

        auto arg1_inserted = try_insert_random();
        if (arg1_inserted.second) return arg1_inserted;
        Ob arg1 = arg1_inserted.first;

        if ((r -= m_injective_prob) < 0) {
            auto & fun = * sample(m_injective_probs, m_injective_prob);
            if (Ob val = fun.find(arg1)) {
                return std::make_pair(val, false);
            } else {
                Ob val = m_carrier.unsafe_insert();
                fun.insert(arg1, val);
                return std::make_pair(val, true);
            }
        }

        auto arg2_inserted = try_insert_random();
        if (arg2_inserted.second) return arg2_inserted;
        Ob arg2 = arg2_inserted.first;

        if ((r -= m_binary_prob) < 0) {
            auto & fun = * sample(m_binary_probs, m_binary_prob);
            if (Ob val = fun.find(arg1, arg2)) {
                return std::make_pair(val, false);
            } else {
                Ob val = m_carrier.unsafe_insert();
                fun.insert(arg1, arg2, val);
                return std::make_pair(val, true);
            }
        }

        if ((r -= m_symmetric_prob) < 0) {
            auto & fun = * sample(m_symmetric_probs, m_symmetric_prob);
            if (Ob val = fun.find(arg1, arg2)) {
                return std::make_pair(val, false);
            } else {
                Ob val = m_carrier.unsafe_insert();
                fun.insert(arg1, arg2, val);
                return std::make_pair(val, true);
            }
        }

        // occasionally fall through due to rounding error
    }
}

} // namespace pomagma
