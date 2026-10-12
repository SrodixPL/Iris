# SafetyHook dependency

Pinned to [SafetyHook v0.7.0](https://github.com/cursey/safetyhook/releases/tag/v0.7.0).
The four amalgamated source/header files are unmodified from the upstream
`safetyhook-amalgamated-zydis.zip` release asset, including static Zydis/Zycore.
Asset SHA-256: `505d4c07ec1c5b94a17f3906ca86afbe1264e738d8becaa244866694c6200c2c`.

SafetyHook uses the Boost Software License in `LICENSE.txt`. Zydis and Zycore
license notices are included in their amalgamated files and must be preserved.

Only SafetyHook and Iris's `inline_hook.cpp` adapter need C++23. The public Iris
hook header remains C++20-compatible. The adapter creates hooks disabled, retains
typed original-function access, and serializes lifecycle changes and duplicate
target registration across runtime owners. Callers must still quiesce callbacks
before closing a hook; stopping threads during patching does not make arbitrary
DLL unload safe.
