// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"

#if DOS_ATTRS_NATIVE == 1
#error "The macro DOS_ATTRS_NATIVE is set to 1, but native DOS attribute support is not available in the POSIX build."
#endif

#if DOS_ATTRS_IN_EXTENDED == 1
#error \
    "The macro DOS_ATTRS_IN_EXTENDED is set to 1, but extended attribute support is not available in the POSIX build."
#endif
