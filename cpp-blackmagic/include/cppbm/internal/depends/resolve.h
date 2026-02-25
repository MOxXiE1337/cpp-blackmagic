// File role:
// Backward-compatible aggregate include for dependency resolution.
//
// New split:
// - resolve_sync.h  : synchronous resolution path
// - resolve_async.h : coroutine async metadata resolution path
//
// Keep including resolve.h from public/internal call sites to preserve
// source compatibility while reducing implementation coupling.

#ifndef __CPPBM_DEPENDS_RESOLVE_H__
#define __CPPBM_DEPENDS_RESOLVE_H__

#include "resolve_sync.h"
#include "resolve_async.h"

#endif // __CPPBM_DEPENDS_RESOLVE_H__
