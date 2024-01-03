/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 2024 MonetDB Foundation;
 * Copyright August 2008 - 2023 MonetDB B.V.;
 * Copyright 1997 - July 2008 CWI.
 */

#ifndef _OPTIMIZER_H
#define _OPTIMIZER_H

#include "mal_interpreter.h"
#include "mal_namespace.h"
#include "opt_support.h"
#include "opt_prelude.h"

extern str optimizer_epilogue(void *ret);
extern str QOToptimize(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci);

#endif /* _OPTIMIZER_H */
