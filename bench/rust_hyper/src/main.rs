// Rust/hyper counterpart to bench/http_hello_mt.logos.
//
// Matches the Logos MT server as closely as the two stacks allow:
//   - N worker threads, each with its own tokio current_thread runtime,
//     its own TcpListener bound with SO_REUSEPORT so the kernel spreads
//     connections across listeners (no cross-thread fd handoff).
//   - GET / => "hello\n" with Content-Length: 6, Content-Type: text/plain
//   - Connection: close on every response.
//   - Port 18080
//
// Built with --no-default-features on tokio + explicit `rt` feature so we
// don't silently end up with the work-stealing multi_thread runtime.

use std::convert::Infallible;
use std::net::SocketAddr;

use bytes::Bytes;
use http_body_util::Full;
use hyper::{service::service_fn, Request, Response};
use hyper_util::rt::TokioIo;
use hyper::server::conn::http1;
use socket2::{Domain, Protocol, Socket, Type};

const PORT: u16 = 18080;
const N_WORKERS: usize = 4;

async fn hello(_req: Request<hyper::body::Incoming>)
    -> Result<Response<Full<Bytes>>, Infallible>
{
    let resp = Response::builder()
        .status(200)
        .header("Content-Type", "text/plain")
        .header("Connection", "close")
        .body(Full::new(Bytes::from_static(b"hello\n")))
        .unwrap();
    Ok(resp)
}

fn make_reuseport_listener(addr: SocketAddr) -> std::io::Result<std::net::TcpListener> {
    let sock = Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?;
    sock.set_reuse_address(true)?;
    sock.set_reuse_port(true)?;
    sock.set_nonblocking(true)?;
    sock.bind(&addr.into())?;
    sock.listen(128)?;
    Ok(sock.into())
}

fn worker(idx: usize) -> std::io::Result<()> {
    let addr: SocketAddr = ([127, 0, 0, 1], PORT).into();
    let std_listener = make_reuseport_listener(addr)?;
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_io()
        .thread_name(format!("hyper-w{idx}"))
        .build()?;
    rt.block_on(async move {
        let listener = tokio::net::TcpListener::from_std(std_listener)?;
        loop {
            let (stream, _) = listener.accept().await?;
            let io = TokioIo::new(stream);
            tokio::spawn(async move {
                let _ = http1::Builder::new()
                    .keep_alive(false)
                    .serve_connection(io, service_fn(hello))
                    .await;
            });
        }
        #[allow(unreachable_code)]
        Ok::<(), std::io::Error>(())
    })
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut handles = Vec::with_capacity(N_WORKERS);
    for i in 0..N_WORKERS {
        handles.push(std::thread::Builder::new()
            .name(format!("hyper-w{i}"))
            .spawn(move || { let _ = worker(i); })?);
    }
    for h in handles { let _ = h.join(); }
    Ok(())
}
