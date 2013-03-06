#include "structure.hpp"
#include "binary_relation.hpp"
#include "nullary_function.hpp"
#include "injective_function.hpp"
#include "binary_function.hpp"
#include "symmetric_function.hpp"
#include <pomagma/util/structure.hpp>
#include <pomagma/util/hdf5.hpp>

namespace pomagma
{

//----------------------------------------------------------------------------
// clearing

void Structure::clear ()
{
    POMAGMA_INFO("Clearing structure data");
    carrier().clear();
    for (auto i : m_signature.binary_relations()) { i.second->clear(); }
    for (auto i : m_signature.nullary_functions()) { i.second->clear(); }
    for (auto i : m_signature.injective_functions()) { i.second->clear(); }
    for (auto i : m_signature.binary_functions()) { i.second->clear(); }
    for (auto i : m_signature.symmetric_functions()) { i.second->clear(); }
}

//----------------------------------------------------------------------------
// loading

// adapted from
// http://www.hdfgroup.org/HDF5/Tutor/crtfile.html

void Structure::load (const std::string & filename)
{
    if (carrier().item_count()) { clear(); }

    POMAGMA_INFO("Loading structure from file " << filename);

    hdf5::init();
    hdf5::InFile file(filename);

    hdf5::Group relations_group(file, "relations");
    hdf5::Group functions_group(file, "functions");

    // TODO parallelize
    load_carrier(file);
    load_binary_relations(file);
    load_nullary_functions(file);
    load_injective_functions(file);
    load_binary_functions(file);
    load_symmetric_functions(file);

    auto digest = hdf5::get_tree_hash(file);
    POMAGMA_ASSERT(digest == hdf5::load_hash(file), "file is corrupt");
}

void Structure::load_carrier (hdf5::InFile & file)
{
    POMAGMA_INFO("loading carrier");

    const std::string groupname = "carrier";
    hdf5::Group group(file, groupname);
    hdf5::Dataset dataset(file, groupname + "/support");

    hdf5::Dataspace dataspace(dataset);
    const auto shape = dataspace.shape();
    POMAGMA_ASSERT_EQ(shape.size(), 1);
    size_t source_item_dim = shape[0] * BITS_PER_WORD - 1;
    size_t destin_item_dim = carrier().item_dim();
    POMAGMA_ASSERT_LE(source_item_dim, destin_item_dim);

    DenseSet support(carrier().item_dim());

    dataset.read_set(support);

    for (auto i = support.iter(); i.ok(); i.next()) {
        carrier().raw_insert(* i);
    }
    carrier().update();
    POMAGMA_ASSERT_EQ(
            carrier().rep_count(),
            carrier().item_count());

    auto digest = get_hash(carrier());
    POMAGMA_ASSERT(digest == hdf5::load_hash(group),
            groupname << " is corrupt");
}

void Structure::load_binary_relations (hdf5::InFile & file)
{
    const std::string groupname = "relations/binary";
    hdf5::Group group(file, groupname);

    for (auto name : group.children()) {
        POMAGMA_ASSERT(m_signature.binary_relations(name),
                "file has unknown " << groupname << "/" << name);
    }

    // TODO parallelize
    for (const auto & pair : m_signature.binary_relations()) {
        std::string name = groupname + "/" + pair.first;
        BinaryRelation * rel = pair.second;
        POMAGMA_INFO("loading " << name);

        size_t dim1 = 1 + rel->item_dim();
        size_t dim2 = rel->round_word_dim();
        auto * destin = rel->raw_data();

        hdf5::Dataset dataset(file, name);
        dataset.read_rectangle(destin, dim1, dim2);
        rel->update();

        auto digest = get_hash(carrier(), * rel);
        POMAGMA_ASSERT(digest == hdf5::load_hash(dataset),
                name << " is corrupt");
    }
}

void Structure::load_nullary_functions (hdf5::InFile & file)
{
    const std::string groupname = "functions/nullary";
    hdf5::Group group(file, groupname);

    for (auto name : group.children()) {
        POMAGMA_ASSERT(m_signature.nullary_functions(name),
                "file has unknown " << groupname << "/" << name);
    }

    for (const auto & pair : m_signature.nullary_functions()) {
        std::string name = groupname + "/" + pair.first;
        NullaryFunction * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Group subgroup(file, name);
        hdf5::Dataset dataset(file, name + "/value");

        Ob data;
        dataset.read_scalar(data);
        fun->raw_insert(data);

        auto digest = get_hash(* fun);
        POMAGMA_ASSERT(digest == hdf5::load_hash(subgroup),
                name << " is corrupt");
    }
}

void Structure::load_injective_functions (hdf5::InFile & file)
{
    if (m_signature.injective_functions().empty()) { return; } // HACK

    const size_t item_dim = carrier().item_dim();
    std::vector<Ob> data(1 + item_dim);

    const std::string groupname = "functions/injective";
    hdf5::Group group(file, groupname);

    for (auto name : group.children()) {
        POMAGMA_ASSERT(m_signature.injective_functions(name),
                "file has unknown " << groupname << "/" << name);
    }

    for (const auto & pair : m_signature.injective_functions()) {
        std::string name = groupname + "/" + pair.first;
        InjectiveFunction * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Group subgroup(file, name);
        hdf5::Dataset dataset(file, name + "/value");

        dataset.read_all(data);

        for (Ob key = 1; key <= item_dim; ++key) {
            if (Ob value = data[key]) {
                fun->raw_insert(key, value);
            }
        }

        auto digest = get_hash(* fun);
        POMAGMA_ASSERT(digest == hdf5::load_hash(dataset),
                name << " is corrupt");
    }
}

namespace detail
{

template<class Function>
inline void load_functions (
        const std::string & arity,
        const std::unordered_map<std::string, Function *> & functions,
        const Carrier & carrier,
        hdf5::InFile & file)
{
    if (functions.empty()) { return; } // HACK

    const size_t item_dim = carrier.item_dim();
    typedef uint_<2 * sizeof(Ob)>::t ptr_t;
    std::vector<ptr_t> lhs_ptr_data(1 + item_dim);
    std::vector<Ob> rhs_data(item_dim);
    std::vector<Ob> value_data(item_dim);

    const std::string groupname = "functions/" + arity;
    hdf5::Group group(file, groupname);

    for (auto name : group.children()) {
        POMAGMA_ASSERT(functions.find(name) != functions.end(),
                "file has unknown " << groupname << "/" << name);
    }

    // TODO parallelize loop
    for (const auto & pair : functions) {
        std::string name = groupname + "/" + pair.first;
        Function * fun = pair.second;
        POMAGMA_INFO("loading " << name);

        hdf5::Group subgroup(file, name);
        hdf5::Dataset lhs_ptr_dataset(file, name + "/lhs_ptr");
        hdf5::Dataset rhs_dataset(file, name + "/rhs");
        hdf5::Dataset value_dataset(file, name + "/value");

        hdf5::Dataspace lhs_ptr_dataspace(lhs_ptr_dataset);
        hdf5::Dataspace rhs_dataspace(rhs_dataset);
        hdf5::Dataspace value_dataspace(value_dataset);

        auto lhs_ptr_shape = lhs_ptr_dataspace.shape();
        POMAGMA_ASSERT_EQ(lhs_ptr_shape.size(), 1);
        POMAGMA_ASSERT_LE(lhs_ptr_shape[0], 1 + item_dim);
        POMAGMA_ASSERT_EQ(rhs_dataspace.shape(), value_dataspace.shape());

        lhs_ptr_dataset.read_all(lhs_ptr_data);
        lhs_ptr_data.push_back(rhs_dataspace.volume());
        for (Ob lhs = 1; lhs < lhs_ptr_data.size() - 1; ++lhs) {
            size_t begin = lhs_ptr_data[lhs];
            size_t end = lhs_ptr_data[lhs + 1];
            POMAGMA_ASSERT_LE(begin, end);
            if (size_t count = end - begin) {

                rhs_data.resize(count);
                rhs_dataset.read_block(rhs_data, begin);

                value_data.resize(count);
                value_dataset.read_block(value_data, begin);

                for (size_t i = 0; i < count; ++i) {
                    fun->raw_insert(lhs, rhs_data[i], value_data[i]);
                }
            }
        }

        auto digest = get_hash(carrier, * fun);
        POMAGMA_ASSERT(digest == hdf5::load_hash(subgroup),
                name << " is corrupt");
    }
}

} // namespace detail

void Structure::load_binary_functions (hdf5::InFile & file)
{
    return detail::load_functions(
            "binary",
            m_signature.binary_functions(),
            carrier(),
            file);
}

void Structure::load_symmetric_functions (hdf5::InFile & file)
{
    return detail::load_functions(
            "symmetric",
            m_signature.symmetric_functions(),
            carrier(),
            file);
}

//----------------------------------------------------------------------------
// dumping

void Structure::dump (const std::string & filename)
{
    POMAGMA_INFO("Dumping structure to file " << filename);
    hdf5::init();
    hdf5::OutFile file(filename);
    pomagma::dump(signature(), file);
}

} // namespace pomagma
