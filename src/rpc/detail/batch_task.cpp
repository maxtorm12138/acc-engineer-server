#include "batch_task.h"
namespace acc_engineer::rpc::detail {
void batch_task_base::cancel()
{
    for (auto &weak_signal : cancellation_signals_)
    {
        if (auto signal = weak_signal.lock(); signal != nullptr)
        {
            signal->emit(net::cancellation_type::all);
        }
    }
}
} // namespace acc_engineer::rpc::detail