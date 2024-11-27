#pragma once

// Headers for ESP32 NimBLE
#include <NimBLEDevice.h>

BEGIN_BLEMIDI_NAMESPACE

class BLEMIDI_ESP32_NimBLE
{
private:
    BLEServer *_server = nullptr;
    BLEAdvertising *_advertising = nullptr;
    BLECharacteristic *_characteristic = nullptr;
    BLECharacteristic *_vendorCharacteristic = nullptr;
    BLECharacteristic *_modelCharacteristic = nullptr;
    BLECharacteristic *_serialNumberCharacteristic = nullptr;
    BLECharacteristic *_hardwareRevisionCharacteristic = nullptr;
    BLECharacteristic *_firmwareRevisionCharacteristic = nullptr;
    BLECharacteristic *_softwareRevisionCharacteristic = nullptr;

    BLEMIDI_Transport<class BLEMIDI_ESP32_NimBLE> *_bleMidiTransport = nullptr;

    friend class MyServerCallbacks;
    friend class MyCharacteristicCallbacks;

protected:
    QueueHandle_t mRxQueue;

public:
    BLEMIDI_ESP32_NimBLE()
    {
    }

    bool begin(const char *, const char *, const char *, BLEMIDI_Transport<class BLEMIDI_ESP32_NimBLE> *);

    void end()
    {
    }

    void write(uint8_t *buffer, size_t length)
    {
        _characteristic->setValue(buffer, length);
        _characteristic->notify();
    }

    bool available(byte *pvBuffer)
    {
        // return 1 byte from the Queue
        return xQueueReceive(mRxQueue, (void *)pvBuffer, 0); // return immediately when the queue is empty
    }

    void add(byte value)
    {
        // called from BLE-MIDI, to add it to a buffer here
        xQueueSend(mRxQueue, &value, portMAX_DELAY);
    }

    NimBLEConnInfo getPeerInfo(size_t index)
    {
        return _server->getPeerInfo(index);
    }

    uint16_t getConnectedCount()
    {
        return _server->getConnectedCount();
    }

    void updateConnParams(uint16_t conn_id, float conn_interval, float conn_timeout, uint16_t conn_latency, uint16_t conn_max_interval)
    {
        _server->updateConnParams(conn_id, conn_interval, conn_timeout, conn_latency, conn_max_interval);
    }

protected:
    void receive(uint8_t *buffer, size_t length)
    {
        // forward the buffer so it can be parsed
        _bleMidiTransport->receive(buffer, length);
    }

    void connected()
    {
        if (_bleMidiTransport->_connectedCallback)
            _bleMidiTransport->_connectedCallback();
    }

    void disconnected()
    {
        if (_bleMidiTransport->_disconnectedCallback)
            _bleMidiTransport->_disconnectedCallback();
    }
};

class MyServerCallbacks : public BLEServerCallbacks
{
public:
    MyServerCallbacks(BLEMIDI_ESP32_NimBLE *bluetoothEsp32)
        : _bluetoothEsp32(bluetoothEsp32)
    {
    }

protected:
    BLEMIDI_ESP32_NimBLE *_bluetoothEsp32 = nullptr;

    void onConnect(BLEServer *)
    {
        if (_bluetoothEsp32)
            _bluetoothEsp32->connected();
            uint16_t conn_id = _bluetoothEsp32->getPeerInfo(0).getConnHandle();
            uint16_t connSize = _bluetoothEsp32->getConnectedCount();
            // Serial.printf("Device connected. Conn ID: %d, Conn Size: %d\n", conn_id, connSize);
            _bluetoothEsp32->updateConnParams(conn_id, 6, 6, 0, 1000);
            uint16_t connInterval = _bluetoothEsp32->getPeerInfo(0).getConnInterval();
            uint16_t connTimeout = _bluetoothEsp32->getPeerInfo(0).getConnTimeout();
            uint16_t connLatency = _bluetoothEsp32->getPeerInfo(0).getConnLatency();
            // Serial.printf("Conn params updated. Conn ID: %d, Conn Interval: %d, Conn Timeout: %d, Conn Latency: %d\n", conn_id, connInterval, connTimeout, connLatency);
    };

    void onDisconnect(BLEServer *)
    {
        if (_bluetoothEsp32)
            _bluetoothEsp32->disconnected();
    }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks
{
public:
    MyCharacteristicCallbacks(BLEMIDI_ESP32_NimBLE *bluetoothEsp32)
        : _bluetoothEsp32(bluetoothEsp32)
    {
    }

protected:
    BLEMIDI_ESP32_NimBLE *_bluetoothEsp32 = nullptr;

    void onWrite(BLECharacteristic *characteristic)
    {
        std::string rxValue = characteristic->getValue();
        if (rxValue.length() > 0)
        {
            _bluetoothEsp32->receive((uint8_t *)(rxValue.c_str()), rxValue.length());
        }
    }
};

bool BLEMIDI_ESP32_NimBLE::begin(const char *deviceName, const char *vendorName, const char *modelName, BLEMIDI_Transport<class BLEMIDI_ESP32_NimBLE> *bleMidiTransport)
{
    _bleMidiTransport = bleMidiTransport;

    BLEDevice::init(deviceName);

    // To communicate between the 2 cores.
    // Core_0 runs here, core_1 runs the BLE stack
    // mRxQueue = xQueueCreate(64, sizeof(uint8_t)); // TODO Settings::MaxBufferSize
    mRxQueue = xQueueCreate(DefaultSettings::MaxBufferSize, sizeof(uint8_t)); // TODO Settings::MaxBufferSize

    _server = BLEDevice::createServer();
    _server->setCallbacks(new MyServerCallbacks(this));
    _server->advertiseOnDisconnect(true);

    // Create the BLE Service
    auto service = _server->createService(BLEUUID(SERVICE_UUID));

    auto deviceInformationService = _server->createService(BLEUUID("180A"));

    // Create a BLE Characteristic
    _characteristic = service->createCharacteristic(
        BLEUUID(CHARACTERISTIC_UUID),
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::NOTIFY |
            NIMBLE_PROPERTY::WRITE_NR);

    _characteristic->setCallbacks(new MyCharacteristicCallbacks(this));

    this->_vendorCharacteristic = deviceInformationService->createCharacteristic((uint16_t) 0x2a29, NIMBLE_PROPERTY::READ);
    this->_vendorCharacteristic->setValue(String(vendorName));


    this->_modelCharacteristic = deviceInformationService->createCharacteristic((uint16_t) 0x2a24, NIMBLE_PROPERTY::READ);
    this->_modelCharacteristic->setValue(String(modelName));
    
    deviceInformationService->start();

    auto _security = new NimBLESecurity();
    _security->setAuthenticationMode(ESP_LE_AUTH_BOND);

    // Start the service
    service->start();

    // Start advertising
    _advertising = _server->getAdvertising();
    _advertising->addServiceUUID(service->getUUID());
    _advertising->addServiceUUID(deviceInformationService->getUUID());
    _advertising->setAppearance(0x00);
    _advertising->start();

    return true;
}

/*! \brief Create an instance for ESP32 named <DeviceName>
 */
#define BLEMIDI_CREATE_INSTANCE(DeviceName, Name)                                                        \
    BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_ESP32_NimBLE> BLE##Name(DeviceName); \
    MIDI_NAMESPACE::MidiInterface<BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_ESP32_NimBLE>, BLEMIDI_NAMESPACE::MySettings> Name((BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_ESP32_NimBLE> &)BLE##Name);

/*! \brief Create a default instance for ESP32 named BLE-MIDI
 */
#define BLEMIDI_CREATE_DEFAULT_INSTANCE() \
    BLEMIDI_CREATE_INSTANCE("Esp32-NimBLE-MIDI", MIDI)

END_BLEMIDI_NAMESPACE
