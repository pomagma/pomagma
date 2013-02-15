#include "structure.hpp"
#include "binary_relation.hpp"
#include "nullary_function.hpp"
#include "injective_function.hpp"
#include "binary_function.hpp"
#include "symmetric_function.hpp"
#include "pomagma_hdf5.hpp"

namespace pomagma
{

//----------------------------------------------------------------------------
// construction

Structure::Structure (Signature & signature)
    : Signature::Observer(signature),
      m_carrier(signature.carrier())
{
    hdf5::init();
}

void Structure::declare (const std::string & name, BinaryRelation & rel)
{
    m_binary_relations[name] = & rel;
}

void Structure::declare (const std::string & name, NullaryFunction & fun)
{
    m_nullary_functions[name] = & fun;
}

void Structure::declare (const std::string & name, InjectiveFunction & fun)
{
    m_injective_functions[name] = & fun;
}

void Structure::declare (const std::string & name, BinaryFunction & fun)
{
    m_binary_functions[name] = & fun;
}

void Structure::declare (const std::string & name, SymmetricFunction & fun)
{
    m_symmetric_functions[name] = & fun;
}

//----------------------------------------------------------------------------
// persistence

// adapted from
// http://www.hdfgroup.org/HDF5/Tutor/crtfile.html

void Structure::load (const std::string & filename)
{
    POMAGMA_INFO("Loading structure from file " << filename);

    hdf5::InFile file(filename);

    // TODO do this in parallel
    // TODO verify SHA1 hash http://www.openssl.org/docs/crypto/sha.html
    load_carrier(file);
    load_binary_relations(file);
    load_nullary_functions(file);
    load_injective_functions(file);
    load_binary_functions(file);
    load_symmetric_functions(file);
}

void Structure::load_carrier (hdf5::InFile & file)
{
    POMAGMA_INFO("loading carrier");

    hdf5::Dataset dataset(file, "/carrier/support");

    hdf5::Dataspace dataspace(dataset);
    const auto shape = dataspace.shape();
    POMAGMA_ASSERT_EQ(shape.size(), 1);
    POMAGMA_ASSERT_LE(shape[0], 1 + MAX_ITEM_DIM);

    DenseSet support(m_carrier.item_dim());
    dataset.read_all(support);

    for (auto i = support.iter(); i.ok(); i.next()) {
        m_carrier.raw_insert(*i);
    }
}

void Structure::load_binary_relations (hdf5::InFile & file)
{
    const std::string prefix = "/relations/binary/";
    // TODO parallelize loop
    for (const auto & pair : m_binary_relations) {
        std::string name = prefix + pair.first;
        BinaryRelation * rel = pair.second;
        POMAGMA_INFO("loading " << name);

        size_t dim1 = 1 + rel->item_dim();
        size_t dim2 = rel->round_word_dim();
        std::atomic<Word> * destin = rel->raw_data();

        hdf5::Dataset dataset(file, name);
        dataset.read_rectangle(destin, dim1, dim2);
        rel->update();
    }
}

void Structure::load_nullary_functions (hdf5::InFile & file)
{
    const std::string prefix = "/functions/nullary/";
    for (const auto & pair : m_nullary_functions) {
        std::string name = prefix + pair.first;
        NullaryFunction * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Dataset dataset(file, name + "/value");

        Ob data;
        dataset.read_scalar(data);
        fun->raw_insert(data);
    }
}

void Structure::load_injective_functions (hdf5::InFile & file)
{
    const size_t item_dim = m_carrier.item_dim();
    std::vector<Ob> data(1 + item_dim);

    const std::string prefix = "/functions/injective/";
    for (const auto & pair : m_injective_functions) {
        std::string name = prefix + pair.first;
        InjectiveFunction * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Dataset dataset(file, name + "/value");

        dataset.read_all(data);

        for (Ob key = 1; key <= item_dim; ++key) {
            Ob value = data[key];
            if (value) {
                fun->insert(key, value);
            }
        }
    }
}

namespace detail
{

template<class Function>
inline void load_functions (
        const std::string & arity,
        std::map<std::string, Function *> functions,
        const Carrier & carrier,
        hdf5::InFile & file)

{
    const size_t item_dim = carrier.item_dim();
    std::vector<hsize_t> lhs_ptr_shape(1, 1 + item_dim);
    std::vector<Ob> lhs_ptr_data(1 + item_dim);
    std::vector<Ob> rhs_data(item_dim);
    std::vector<Ob> value_data(item_dim);

    const std::string prefix = "/functions/" + arity + "/";
    // TODO parallelize loop
    for (const auto & pair : functions) {
        std::string name = prefix + pair.first;
        Function * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Dataset lhs_ptr_dataset(file, name + "/lhs_ptr");
        hdf5::Dataset rhs_dataset(file, name + "/rhs");
        hdf5::Dataset value_dataset(file, name + "/value");

        hdf5::Dataspace lhs_ptr_dataspace(lhs_ptr_dataset);
        hdf5::Dataspace rhs_dataspace(rhs_dataset);
        hdf5::Dataspace value_dataspace(value_dataset);

        POMAGMA_ASSERT_EQ(lhs_ptr_dataspace.shape(), lhs_ptr_shape);
        POMAGMA_ASSERT_EQ(rhs_dataspace.shape(), value_dataspace.shape());

        lhs_ptr_dataset.read_all(lhs_ptr_data);
        lhs_ptr_data.push_back(rhs_dataspace.volume());
        for (Ob lhs = 1; lhs <= item_dim; ++lhs) {
            size_t begin = lhs_ptr_data[lhs];
            size_t end = lhs_ptr_data[lhs + 1];
            size_t count = end - begin;
            if (count) {

                rhs_data.resize(count);
                value_data.resize(count);

                rhs_dataset.read_block(rhs_data, begin);
                value_dataset.read_block(value_data, begin);

                for (size_t i = 0; i < count; ++i) {
                    fun->insert(lhs, rhs_data[i], value_data[i]);
                }
            }
        }
        lhs_ptr_data.pop_back();
    }
}

} // namespace detail

void Structure::load_binary_functions (hdf5::InFile & file)
{
    detail::load_functions("binary", m_binary_functions, m_carrier, file);
}

void Structure::load_symmetric_functions (hdf5::InFile & file)
{
    detail::load_functions("symmetric", m_symmetric_functions, m_carrier, file);
}

void Structure::dump (const std::string & filename)
{
    POMAGMA_INFO("Dumping structure to file " << filename);
    hdf5::OutFile file(filename);

    // TODO do this in parallel
    // TODO compute SHA1 hash http://www.openssl.org/docs/crypto/sha.html
    dump_carrier(file);
    dump_binary_relations(file);
    dump_nullary_functions(file);
    dump_injective_functions(file);
    dump_binary_functions(file);
    dump_symmetric_functions(file);
}

void Structure::dump_carrier (hdf5::OutFile & file)
{
    POMAGMA_INFO("dumping carrier");

    const DenseSet & support = m_carrier.support();

    hdf5::Dataspace dataspace(support.word_dim());

    hdf5::Dataset dataset(
            file,
            "/carrier/support",
            hdf5::Bitfield<Word>::id(),
            dataspace);

    dataset.write_all(support);
}

void Structure::dump_binary_relations (hdf5::OutFile & file)
{
    const DenseSet & support = m_carrier.support();
    const size_t item_dim = support.item_dim();
    const size_t word_dim = support.word_dim();

    hdf5::Dataspace dataspace(1 + item_dim, word_dim);

    const std::string prefix = "/relations/binary/";
    // TODO parallelize loop
    for (const auto & pair : m_binary_relations) {
        std::string name = prefix + pair.first;
        const BinaryRelation * rel = pair.second;
        POMAGMA_INFO("dumping " << name);

        hdf5::Dataset dataset(
                file,
                name,
                hdf5::Unsigned<Ob>::id(),
                dataspace);

        size_t dim1 = 1 + rel->item_dim();
        size_t dim2 = rel->round_word_dim();
        const std::atomic<Word> * source = rel->raw_data();

        dataset.write_rectangle(source, dim1, dim2);
    }
}

void Structure::dump_nullary_functions (hdf5::OutFile & file)
{
    hdf5::Dataspace dataspace;

    const std::string prefix = "/functions/nullary/";
    for (const auto & pair : m_nullary_functions) {
        std::string name = prefix + pair.first;
        const NullaryFunction * fun = pair.second;
        POMAGMA_INFO("dumping " << name);

        hdf5::Dataset dataset(
                file,
                name + "/value",
                hdf5::Unsigned<Ob>::id(),
                dataspace);

        Ob data = fun->find();
        dataset.write_scalar(data);
    }
}

void Structure::dump_injective_functions (hdf5::OutFile & file)
{
    // format:
    // dense array with null entries

    const size_t item_dim = m_carrier.item_dim();
    hdf5::Dataspace dataspace(1 + item_dim);

    const std::string prefix = "/functions/injective/";
    for (const auto & pair : m_injective_functions) {
        std::string name = prefix + pair.first;
        const InjectiveFunction * fun = pair.second;
        POMAGMA_INFO("dumping " << name);

        hdf5::Dataset dataset(
                file,
                name + "/value",
                hdf5::Unsigned<Ob>::id(),
                dataspace);

        std::vector<Ob> data(1 + item_dim, 0);
        for (auto key = fun->iter(); key.ok(); key.next()) {
            data[*key] = fun->raw_find(*key);
        }

        dataset.write_all(data);
    }
}

namespace detail
{

template<class Function>
inline void dump_functions (
        const std::string & arity,
        std::map<std::string, Function *> functions,
        const Carrier & carrier,
        hdf5::OutFile & file)
{
    // format:
    // compressed sparse row (CSR) matrix

    const size_t item_dim = carrier.item_dim();

    typedef uint32_t ptr_t;
    static_assert(sizeof(ptr_t) == 2 * sizeof(Ob), "bad ptr type");
    auto ptr_type = hdf5::Unsigned<ptr_t>::id();
    auto ob_type = hdf5::Unsigned<Ob>::id();

    hdf5::Dataspace ptr_dataspace(1 + item_dim);

    std::vector<ptr_t> lhs_ptr_data(1 + item_dim);
    std::vector<Ob> rhs_data(1 + item_dim);
    std::vector<Ob> value_data(1 + item_dim);

    const std::string prefix = "/functions/" + arity + "/";
    // TODO parallelize loop
    for (const auto & pair : functions) {
        std::string name = prefix + pair.first;
        const Function * fun = pair.second;
        POMAGMA_INFO("dumping " << name);

        size_t pair_count = fun->count_pairs();
        hdf5::Dataspace ob_dataspace(pair_count);

        hdf5::Dataset lhs_ptr_dataset(
                file,
                name + "/lhs_ptr",
                ptr_type,
                ptr_dataspace);
        hdf5::Dataset rhs_dataset(
                file,
                name + "/rhs",
                ob_type,
                ob_dataspace);
        hdf5::Dataset value_dataset(
                file,
                name + "/value",
                ob_type,
                ob_dataspace);

        lhs_ptr_data[0] = 0;
        rhs_data.clear();
        value_data.clear();
        for (Ob lhs = 1; lhs <= item_dim; ++lhs) {
            ptr_t begin = lhs_ptr_data[lhs - 1] + value_data.size();
            lhs_ptr_data[lhs] = begin;
            rhs_data.clear();
            value_data.clear();
            if (carrier.contains(lhs)) {
                for (auto rhs = fun->iter_lhs(lhs); rhs.ok(); rhs.next()) {
                    if (Function::is_symmetric() and *rhs > lhs) { break; }
                    rhs_data.push_back(*rhs);
                    value_data.push_back(fun->raw_find(lhs, *rhs));
                }
                rhs_dataset.write_block(rhs_data, begin);
                value_dataset.write_block(value_data, begin);
            }
        }
        ptr_t end = lhs_ptr_data[item_dim] + value_data.size();
        POMAGMA_ASSERT_EQ(end, pair_count);

        lhs_ptr_dataset.write_all(lhs_ptr_data);
    }
}

} // namespace detail

void Structure::dump_binary_functions (hdf5::OutFile & file)
{
    detail::dump_functions("binary", m_binary_functions, m_carrier, file);
}

void Structure::dump_symmetric_functions (hdf5::OutFile & file)
{
    detail::dump_functions("symmetric", m_symmetric_functions, m_carrier, file);
}

} // namespace pomagma