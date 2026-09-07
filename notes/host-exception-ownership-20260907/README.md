# Host-owned standard exception messages

`aurora::throw_host_exception<Exception>(string_view)` copies and constructs a
standard message-bearing exception under `HostAllocationScope`. It restores
the entire prior routing state during unwinding and preserves the exact
exception type and message. Custom exceptions are deliberately outside this
API. Taking a message avoids sharing an existing exception's client-owned
storage. Normal client allocations retain their original routing.

The embedding Petari test `smg-pc-jkr-exception-ownership-tests` builds and runs
successfully against this header and real JKR/global-new routing. It covers all
nine standard classes, substring messages, nested Host/Client and explicit
client arenas, original allocations after catching, copies/exception_ptr and
rethrow after full arena retirement and replacement memory overwrite. A real
camera error is caught after its scene arena is gone and retains its original
type/message. The small unfixed probe records libc++ allocating the exception
message in the client heap and libc++abi subsequently freeing retired storage.

Full transcripts, test source and production caller migration are in the
embedding repository's `notes/jkr-exception-ownership-20260907/`. Library-internal
and third-party error constructors still require an ownership audit; this API
does not intercept global exception construction.
