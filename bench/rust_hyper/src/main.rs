// Rust/hyper counterpart to bench/http_hello.logos.
//
// Matches the Logos server as closely as the two stacks allow:
//   - single-threaded runtime (tokio current_thread), one accept loop
//   - GET / => "hello\n" with Content-Length: 6, Content-Type: text/plain
//   - Connection: close on every response (same as Logos bench)
//   - Port 18080
//
// Keeping the "close per response" policy is what lets the ab numbers be
// compared directly: both servers pay the full accept+parse+send+close
// cost for every request, no HTTP/1.1 keep-alive amortisation on either
// side.

use std::convert::Infallible;
use std::net::SocketAddr;

use bytes::Bytes;
use http_body_util::Full;
use hyper::{service::service_fn, Request, Response};
use hyper_util::rt::TokioIo;
use hyper::server::conn::http1;

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

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_io()
        .build()?;

    rt.block_on(async {
        let addr: SocketAddr = ([127, 0, 0, 1], 18080).into();
        let listener = tokio::net::TcpListener::bind(addr).await?;
        loop {
            let (stream, _) = listener.accept().await?;
            let io = TokioIo::new(stream);
            tokio::spawn(async move {
                // http1, no keep-alive — matches Logos bench semantics.
                let _ = http1::Builder::new()
                    .keep_alive(false)
                    .serve_connection(io, service_fn(hello))
                    .await;
            });
        }
        #[allow(unreachable_code)]
        Ok::<(), Box<dyn std::error::Error>>(())
    })?;

    Ok(())
}
