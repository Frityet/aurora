# Host-owned standard exception messages

`aurora::throw_host_exception<Exception>(string_view)` copies and constructs a
standard message-bearing exception under `HostAllocationScope`. It restores
the entire prior routing state during unwinding and preserves the exact
exception type and message. Custom exceptions are deliberately outside this
API. Taking a message avoids sharing an existing exception's client-owned
storage. Normal client allocations retain their original routing.

The subsequent adoption checkpoint migrates all 68 explicit standard-message
throws in six Aurora-owned host files: audio, JAudio stream/archive parsers,
SYSCONF, BRLAN parsing and the native JAISoundHandle backend validation. Only
the throwing prefix and include change; reversing these edits reproduces every
original source hash. Original retail SDK exceptions and custom types are not
rewritten.

The embedding Petari test `smg-pc-jkr-exception-ownership-tests` builds and runs
successfully against this header and real JKR/global-new routing. It covers all
nine standard classes, substring messages, nested Host/Client and explicit
client arenas, original allocations after catching, copies/exception_ptr and
rethrow after full arena retirement and replacement memory overwrite. A real
camera error is caught after its scene arena is gone and retains its original
type/message. The small unfixed probe records libc++ allocating the exception
message in the client heap and libc++abi subsequently freeing retired storage.

After caller adoption, the same test builds/runs again and additionally invokes
actual invalid BRLAN, SYSCONF and JAISoundHandle APIs under a Game arena. Each
error is caught after that arena is destroyed, with its exact type and retained
host-owned message. All five changed .cpp files and the JAISound header pass
isolated native syntax checks. Audio mixing/playback runtime was not retested.

Full transcripts, test source and production caller migration are in the
embedding repository's `notes/jkr-exception-ownership-20260907/`. Library-internal
and third-party error constructors still require an ownership audit; this API
does not intercept global exception construction.
