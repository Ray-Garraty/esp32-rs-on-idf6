#include "domain/calibration.hpp"
#include "infrastructure/cal_cache.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"
#include "infrastructure/motor_task.hpp"

// Non-null sentinel — xQueueSend stub always returns pdTRUE
static int s_queueSentinel;
QueueHandle_t ecotiter::infrastructure::gMotorCmdQueue = &s_queueSentinel;
QueueHandle_t ecotiter::infrastructure::gSmResultQueue = &s_queueSentinel;
ecotiter::infrastructure::drivers::TmcUart ecotiter::infrastructure::gTmcUart;

// Calibration cache for host tests — matches default values from nvs.cpp:259
// Heap-allocated so AtomicOwner can own it (no destructor clash)
struct CalCacheInit
{
    CalCacheInit()
    {
        auto* cal = new ecotiter::domain::CalibrationData(
            ecotiter::domain::CalibrationData::kDefaultStepsPerMl,
            ecotiter::domain::CalibrationData::kDefaultNominalVolumeMl,
            ecotiter::domain::CalibrationData::kDefaultSpeedCoeff,
            ecotiter::domain::CalibrationData::kDefaultMinFreqHz,
            ecotiter::domain::CalibrationData::kDefaultMaxFreqHz);
        ecotiter::infrastructure::gCalCache.store(cal, std::memory_order_release);
    }
    ~CalCacheInit()
    {
        // Clear gCalCache before AtomicOwner destructor to avoid double-delete
        delete ecotiter::infrastructure::gCalCache.exchange(nullptr,
                                                            std::memory_order_acq_rel);
    }
};
static CalCacheInit s_calInit;
