#pragma once
// The toolchain version, defined in ONE generated translation unit
// (build/src/compiler/version.cpp, from src/compiler/version.cpp.in). A version
// change recompiles that file alone; it used to be a -D on every logosc source.

namespace logos::compiler {
// X.Y.Z[-pre][+disc]: what `--version` reports, the @abi stamp of an archive,
// and the runtime reuse check compares.
const char* logos_version_full() noexcept;
// The install slot, X.Y[-pre][-disc].
const char* logos_version_slot() noexcept;
}  // namespace logos::compiler
