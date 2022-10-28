#include "batch_task.h"
namespace acc_engineer::rpc::detail {
void batch_task_base::cancel()
{
    for (auto &sig : cancellation_signals_)
    {
        sig->emit(net::cancellation_type::all);
    }
}
} // namespace acc_engineer::rpc::detail