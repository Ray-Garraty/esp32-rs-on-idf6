#pragma once

namespace ecotiter::infrastructure::network
{

class BleManager;

void startBleNotifyThread(BleManager& manager);

} // namespace ecotiter::infrastructure::network
