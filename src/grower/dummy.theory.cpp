#include "util.hpp"
#include "carrier.hpp"
#include "sampler.hpp"
#include "scheduler.hpp"

namespace pomagma
{

#define DEF_EXECUTE(POMAGMA_name)\
    void execute (const POMAGMA_name &)\
    { POMAGMA_INFO("executing " #POMAGMA_name); }

DEF_EXECUTE(MergeTask)
DEF_EXECUTE(ExistsTask)
DEF_EXECUTE(PositiveOrderTask)
DEF_EXECUTE(NegativeOrderTask)
DEF_EXECUTE(NullaryFunctionTask)
DEF_EXECUTE(InjectiveFunctionTask)
DEF_EXECUTE(BinaryFunctionTask)
DEF_EXECUTE(SymmetricFunctionTask)
DEF_EXECUTE(CleanupTask)
DEF_EXECUTE(SampleTask)

#undef DEF_EXECUTE

void set_language_prob (const std::string & name, float prob)
{
    POMAGMA_INFO("set p(" << name << ") = " << prob);
}

void assume_core_facts ()
{
    POMAGMA_INFO("assume_core_facts()");
}

void cleanup_tasks_push_all ()
{
    POMAGMA_INFO("cleanup_tasks_push_all()");
}

bool cleanup_tasks_try_pop (CleanupTask &)
{
    POMAGMA_INFO("cleanup_tasks_try_pop()");
    return false;
}

bool sample_tasks_try_pop (SampleTask &)
{
    POMAGMA_INFO("sample_tasks_try_pop()");
    return false;
}

void validate_all ()
{
    POMAGMA_INFO("validate_all()");
}

} // namespace pomagma