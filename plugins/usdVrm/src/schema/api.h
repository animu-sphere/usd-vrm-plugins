//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_API_H
#define USDVRM_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDVRM_API
#   define USDVRM_API_TEMPLATE_CLASS(...)
#   define USDVRM_API_TEMPLATE_STRUCT(...)
#   define USDVRM_LOCAL
#else
#   if defined(USDVRM_EXPORTS)
#       define USDVRM_API ARCH_EXPORT
#       define USDVRM_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDVRM_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDVRM_API ARCH_IMPORT
#       define USDVRM_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDVRM_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDVRM_LOCAL ARCH_HIDDEN
#endif

#endif
