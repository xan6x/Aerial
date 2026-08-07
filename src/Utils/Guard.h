#pragma once

namespace aerial {

using GuardedFn = void (*)(void*);

bool runGuarded(const char* what, GuardedFn fn, void* data);

template <typename F>
inline bool guarded(const char* what, F callable) {
    const GuardedFn trampoline = +[](void* data) { (*static_cast<F*>(data))(); };
    return runGuarded(what, trampoline, &callable);
}

}
