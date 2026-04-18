#pragma once
#include <string>
#include <functional>

using LineCallback = std::function<void(const std::string&)>;

bool initBle(const std::string& nameSuffix, LineCallback onLine);
void pollBle();
bool isBleConnected();
bool sendLine(const std::string& line);  // should include trailing '\n'
