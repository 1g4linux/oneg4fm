/*
 * GIO-backed file-operations executor for unified contract
 * src/core/gio_ops_executor.h
 */

#ifndef PCMANFM_GIO_OPS_EXECUTOR_H
#define PCMANFM_GIO_OPS_EXECUTOR_H

#include "file_ops_contract.h"

namespace Oneg4FM::FileOpsContract::detail {

Result run_gio_request(const Request& request, const EventHandlers& handlers);

}  // namespace Oneg4FM::FileOpsContract::detail

#endif  // PCMANFM_GIO_OPS_EXECUTOR_H
