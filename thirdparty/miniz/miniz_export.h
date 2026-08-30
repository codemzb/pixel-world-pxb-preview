// miniz_export.h - stub for static (non-DLL) builds.
// The upstream miniz.h unconditionally includes this; for a static link we
// want no symbol decoration.
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H
#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT
#define MINIZ_EXPORT_IMPORT
#endif
