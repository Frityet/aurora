#pragma once

namespace aurora::diagnostics {

// Install the process-wide C++ terminate reporter. Repeated installation is
// harmless. The handler retains no client resources and remains usable after
// client heaps, rendering and worker services have been retired. Fatal native
// signals retain the operating system's existing handling.
void install_native_exception_reporting() noexcept;

} // namespace aurora::diagnostics
