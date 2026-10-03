/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keep the existing CI entry point. The 0.7.1.6 successor suite still runs
 * the original ownership/pressure fixtures, all 4225 queue/credit pairs, clock
 * rollback, BUSY/fault, cancellation/lifecycle/flow and poisoned-NBL cases.
 * The deliberate expectation change is NO extra low-pressure frames in either
 * mode; the previously experimental cases now exercise the baseline extension.
 */
#include "tx_dispatch_only_tests.c"
