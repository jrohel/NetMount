// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#ifndef DOS_ATTRS_NATIVE
#if defined(__linux__) || defined(_WIN32) || defined(__FreeBSD__)
#define DOS_ATTRS_NATIVE 1
#else
#define DOS_ATTRS_NATIVE 0
#endif
#endif

#ifndef DOS_ATTRS_IN_EXTENDED
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
#define DOS_ATTRS_IN_EXTENDED 1
#else
#define DOS_ATTRS_IN_EXTENDED 0
#endif
#endif

#endif
