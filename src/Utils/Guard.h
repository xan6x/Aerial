#pragma once

namespace aerial {

// Runs `fn` under a structured-exception handler. On a fault the address and
// exception code are logged (a few times per site, then suppressed) and the
// call returns false instead of taking the game down.
//
// This exists because every offset in the SDK is a hypothesis: a wrong one
// dereferences garbage, and during bring-up a logged fault is far more useful
// than a crash dump. It is not a substitute for correct offsets - anything that
// trips this should be fixed.
using GuardedFn = void (*)(void*);

bool runGuarded(const char* what, GuardedFn fn, void* data);

template <typename F>
inline bool guarded(const char* what, F callable) {
    const GuardedFn trampoline = +[](void* data) { (*static_cast<F*>(data))(); };
    return runGuarded(what, trampoline, &callable);
}

} // namespace aerial
