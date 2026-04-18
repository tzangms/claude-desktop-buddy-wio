#include "ble_nus.h"
#include "config.h"
#include <Arduino.h>
#include <rpcBLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

static BLECharacteristic* txChar = nullptr;
static BLECharacteristic* rxChar = nullptr;
static LineCallback onLineCb;
static std::string rxBuf;
static bool connected = false;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override    { connected = true;  }
  void onDisconnect(BLEServer* s) override {
    connected = false;
    rxBuf.clear();
    s->getAdvertising()->start();
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    rxBuf.append(v);
    size_t pos;
    while ((pos = rxBuf.find('\n')) != std::string::npos) {
      std::string line = rxBuf.substr(0, pos);
      rxBuf.erase(0, pos + 1);
      if (onLineCb) onLineCb(line);
    }
    if (rxBuf.size() > 8192) {
      Serial.println("[BLE] rx overflow, clearing buffer");
      rxBuf.clear();
    }
  }
};

bool initBle(const std::string& nameSuffix, LineCallback onLine) {
  onLineCb = onLine;
  std::string name = std::string(DEVICE_NAME_PREFIX) + nameSuffix;
  BLEDevice::init(name);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService* svc = server->createService(NUS_SERVICE_UUID);

  txChar = svc->createCharacteristic(
      NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->setAccessPermissions(GATT_PERM_READ);
  txChar->addDescriptor(new BLE2902());

  rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setAccessPermissions(GATT_PERM_READ | GATT_PERM_WRITE);
  rxChar->setCallbacks(new RxCB());

  svc->start();

  // Primary: Flags (3B) + 128-bit service UUID (18B) + shortened name
  // "Claude" (8B) = 29B, within the 31-byte budget. Central scanners that
  // filter on either service UUID or name prefix in the primary packet
  // will see us. Full device name goes in the scan response.
  BLEAdvertisementData advData;
  advData.setFlags(0x06);  // General Discoverable + BR/EDR not supported
  advData.setCompleteServices(BLEUUID(NUS_SERVICE_UUID));
  // Shortened Local Name tag 0x08 signals "a longer name lives in scan resp".
  const char shortNameAd[] = {7, 0x08, 'C', 'l', 'a', 'u', 'd', 'e'};
  advData.addData(std::string(shortNameAd, sizeof(shortNameAd)));

  BLEAdvertisementData scanResp;
  scanResp.setName(name);

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanResp);
  adv->start();
  Serial.print("BLE advertising as: "); Serial.println(name.c_str());
  return true;
}

void pollBle() {
  // rpcBLE is event-driven; nothing to poll.
}

bool isBleConnected() { return connected; }

bool sendLine(const std::string& line) {
  if (!connected || !txChar) return false;
  txChar->setValue((uint8_t*)line.data(), line.size());
  txChar->notify();
  return true;
}
