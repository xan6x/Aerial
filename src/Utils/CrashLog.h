#pragma once

namespace aerial::crash {

// A vectored exception handler that writes the faulting address, the exception
// code and the owning module to the log before the process dies.
//
// It exists because "the game crashed" is not a diagnosis. The one fact that
// separates a client bug from a game bug is whether the faulting instruction is
// inside AerialClient.dll, and nothing else in the log answers that.
//
// Vectored, not structured: the handler runs before the game's own __try blocks
// and before any unwinding, so the report describes the original fault rather
// than whatever the game turned it into. It only reports - the exception is
// always passed on untouched.
void install();
void remove();

} // namespace aerial::crash
