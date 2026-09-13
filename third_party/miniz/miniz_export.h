/* Vendored stand-in for the CMake-generated miniz_export.h (upstream's
   generate_export_header output). miniz compiles straight into libtask_graph
   on every platform; symbol hygiene is handled in CMake instead
   (-fvisibility=hidden on the .c sources for shared desktop builds), so the
   visibility macros are identity. */
#pragma once

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT
#define MINIZ_DEPRECATED
