#include "server.hpp"

int main (int argc, char ** argv)
{
    pomagma::Log::Context log_context(argc, argv);

    if (argc != 5) {
        std::cout
            << "Usage: "
                << pomagma::get_filename(argv[0])
                << " structure theory language" << "\n"
            << "Environment Variables:\n"
            << "  POMAGMA_LOG_FILE = " << pomagma::DEFAULT_LOG_FILE << "\n"
            << "  POMAGMA_LOG_LEVEL = " << pomagma::DEFAULT_LOG_LEVEL << "\n"
            ;
        POMAGMA_WARN("incorrect program args");
        exit(1);
    }

    const char * structure_file = argv[1];
    const char * theory_file = argv[2];
    const char * language_file = argv[3];
    const char * address = argv[4];

    // load structure
    pomagma::Structure structure;
    structure.load(structure_file);
    if (POMAGMA_DEBUG_LEVEL > 1) {
        structure.validate();
    }

    // serve
    pomagma::Server server(structure, theory_file, language_file);
    server.serve(address);

    return 0;
}
