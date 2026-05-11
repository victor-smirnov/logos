//
// Local HRPC transport — zero-copy cross-reactor RPC without TCP.
//
// LocalServer holds an EndpointRegistry on a specific reactor.
// LocalServer::call() posts a handler invocation to the server reactor via
// submit_to(), moving the Request document without serialization.
//
// Usage:
//
//   // Setup (on any thread, before reactors start):
//   LocalServer server(engine.reactor(1));
//   server.endpoints().add(endpoint_id, handler_fn).get();
//
//   // Client fiber on reactor 0:
//   auto response = server.call(endpoint_id, std::move(request));
//   if (!response) { /* handle error */ }
//   if (response->is_ok()) { /* use response->result() */ }
//
// Memory model:
//   - Request document is moved to the server reactor (zero-copy).
//   - Response document is moved back to the client reactor (zero-copy).
//   - No serialization / deserialization — documents cross reactor boundaries
//     as moved Own<Hermes> with atomic refcount only.
//   - The calling fiber is suspended for the duration of the handler.

#pragma once

#include <logos/hrpc/context.hpp>
#include <logos/hrpc/endpoint.hpp>
#include <logos/hrpc/schema.hpp>
#include <logos/reactor/reactor.hpp>
#include <logos/reactor/submit_to.hpp>

namespace logos::hrpc {

// ---------------------------------------------------------------------------
// LocalServer — holds an EndpointRegistry for a specific reactor.
//
// Register endpoints before the reactor starts running. The registry is
// accessed only from the server reactor's thread (inside handler fibers
// posted by call() via submit_to).
//
// call() may be invoked from any reactor in the same ReactorEngine.
// ---------------------------------------------------------------------------
class LocalServer {
public:
    explicit LocalServer(logos::reactor::Reactor& reactor) noexcept
        : reactor_(&reactor) {}

    EndpointRegistry& endpoints() noexcept { return endpoints_; }
    logos::reactor::Reactor& reactor() noexcept { return *reactor_; }

    // Cross-reactor HRPC call via submit_to (zero-copy).
    //
    // Posts the handler invocation to this server's reactor. The calling
    // fiber is suspended until the handler completes and the Response is
    // moved back.
    //
    // Must be called from a fiber on a reactor in the same ReactorEngine.
    [[nodiscard]]
    logos::expected<Response>
    call(const EndpointID& endpoint_id, Request request) noexcept {
        return logos::reactor::submit_to(*reactor_,
            [this, endpoint_id, req = std::move(request)]() mutable noexcept
            -> logos::expected<Response>
        {
            return dispatch_(endpoint_id, std::move(req));
        });
    }

private:
    // Runs on the server reactor's thread (green fiber context).
    logos::expected<Response>
    dispatch_(const EndpointID& endpoint_id, Request req) noexcept {
        HandlerFn* handler = endpoints_.get(endpoint_id);
        if (!handler)
            return Response::error("unknown endpoint");

        Context ctx;
        ctx.set_request(std::move(req));
        ctx.set_endpoint_id(endpoint_id);
        ctx.set_call_id(0);
        ctx.set_session(nullptr);

        return (*handler)(ctx);
    }

    logos::reactor::Reactor* reactor_;
    EndpointRegistry         endpoints_;
};

} // namespace logos::hrpc
