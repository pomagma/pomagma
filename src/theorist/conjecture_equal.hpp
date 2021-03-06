#pragma once

#include <pomagma/macrostructure/util.hpp>
#include <pomagma/macrostructure/structure.hpp>
#include <pomagma/macrostructure/router.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace pomagma
{

static const size_t DEFAULT_CONJECTURE_COUNT = 1000;
static const size_t DEFAULT_PROOF_COUNT = 100;

size_t conjecture_equal (
        Structure & structure,
        const char * language_file,
        const char * conjectures_file,
        size_t max_count = DEFAULT_CONJECTURE_COUNT);

size_t conjecture_equal (
        Structure & structure,
        const std::vector<float> & probs,
        const std::vector<std::string> & routes,
        const char * conjectures_file,
        size_t max_count = DEFAULT_CONJECTURE_COUNT);

void try_prove_nless (
        Structure & structure,
        const char * language_file,
        const char * conjectures_in_file,
        const char * conjectures_out_file,
        const char * theorems_file,
        size_t max_count = DEFAULT_PROOF_COUNT);

} // namespace pomagma
