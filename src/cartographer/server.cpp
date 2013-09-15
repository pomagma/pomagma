#include "server.hpp"
#include "trim.hpp"
#include "aggregate.hpp"
#include "infer.hpp"
#include "signature.hpp"
#include <pomagma/macrostructure/carrier.hpp>
#include <pomagma/macrostructure/structure.hpp>
#include <pomagma/macrostructure/compact.hpp>
#include "messages.pb.h"
#include <zmq.hpp>

namespace pomagma
{

Server::Server (
        Structure & structure,
        const char * theory_file,
        const char * language_file)
    : m_structure(structure),
      m_theory_file(theory_file),
      m_language_file(language_file)
{
}


void Server::trim (
        size_t region_size,
        const std::vector<std::string> & regions_out)
{
    size_t region_count = regions_out.size();
    if (m_structure.carrier().item_dim() <= region_size) {
        compact(m_structure);
        #pragma omp parallel for schedule(dynamic, 1)
        for (size_t iter = 0; iter < region_count; ++iter) {
            m_structure.dump(regions_out[iter]);
        }
    } else {
        #pragma omp parallel for schedule(dynamic, 1)
        for (size_t iter = 0; iter < region_count; ++iter) {
            Structure region;
            region.init_carrier(region_size);
            extend(region.signature(), m_structure.signature());
            pomagma::trim(m_structure, region, m_theory_file, m_language_file);
            region.dump(regions_out[iter]);
        }
    }
}

void Server::aggregate (const std::string & survey_in)
{
    Structure survey;
    survey.load(survey_in);
    DenseSet defined = restricted(survey.signature(), m_structure.signature());
    compact(m_structure);
    size_t total_dim =
        m_structure.carrier().item_count() + defined.count_items();
    if (m_structure.carrier().item_dim() < total_dim) {
        m_structure.resize(total_dim);
    }
    pomagma::aggregate(m_structure, survey, defined);
}

size_t Server::infer ()
{
    return infer_lazy(m_structure);
}

void Server::crop ()
{
    compact(m_structure);
    size_t item_count = m_structure.carrier().item_count();
    if (item_count < m_structure.carrier().item_dim()) {
        m_structure.resize(item_count);
    }
}

void Server::validate ()
{
    if (POMAGMA_DEBUG_LEVEL > 1) {
        m_structure.validate();
    } else {
        m_structure.validate_consistent();
    }
}

void Server::dump (const std::string & world_out)
{
    pomagma::compact(m_structure);
    m_structure.log_stats();
    m_structure.dump(world_out);
}


namespace
{

messaging::CartographerResponse handle (
    Server & server,
    messaging::CartographerRequest & request)
{
    POMAGMA_INFO("Handling request");
    messaging::CartographerResponse response;

    if (request.has_trim()) {
        const size_t region_size = request.trim().size();
        std::vector<std::string> regions_out;
        for (int i = 0; i < request.trim().regions_out_size(); ++i) {
            regions_out.push_back(request.trim().regions_out(i));
        }
        server.trim(region_size, regions_out);
        response.mutable_trim();
    }

    if (request.has_aggregate()) {
        const size_t survey_count = request.aggregate().surveys_in_size();
        for (size_t iter = 0; iter < survey_count; ++iter) {
            server.aggregate(request.aggregate().surveys_in(iter));
        }
        response.mutable_aggregate();
    }

    if (request.has_infer()) {
        const size_t theorem_count = server.infer();
        response.mutable_infer()->set_theorem_count(theorem_count);
    }

    if (request.has_crop()) {
        server.crop();
        response.mutable_crop();
    }

    if (request.has_validate()) {
        server.validate();
        response.mutable_validate();
    }

    if (request.has_dump()) {
        server.dump(request.dump().world_out());
        response.mutable_dump();
    }

    return response;
}

} // anonymous namespace

void Server::serve (const char * address)
{
    POMAGMA_INFO("Starting server");
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind(address);

    while (true) {
        POMAGMA_DEBUG("waiting for request");
        zmq::message_t raw_request;
        socket.recv(& raw_request);

        POMAGMA_DEBUG("parsing request");
        messaging::CartographerRequest request;
        request.ParseFromArray(raw_request.data(), raw_request.size());

        messaging::CartographerResponse response = handle(* this, request);

        POMAGMA_DEBUG("serializing response");
        std::string response_str;
        response.SerializeToString(& response_str);
        const size_t size = response_str.length();
        zmq::message_t raw_response(size);
        memcpy(raw_response.data(), response_str.c_str(), size);

        POMAGMA_DEBUG("sending response");
        socket.send(raw_response);
    }
}

} // namespace pomagma