# ESPHome Components

This repository contains custom [ESPHome](https://esphome.io/) components for devices developed by IoTLabs.

## `socket_transmitter`

This component is used to send data to a remote server using a socket connection. It supports both TCP and UDP protocols. Especially is made for sending wM-Bus packets to a remote server for further processing.

**Example configuration:**

```yaml
socket_transmitter:
  id: transmitter
  ip_address: <remote_host>
  port: <remote_port>
  protocol: <tcp|udp>

interval:
  - interval: 10s
    then:
      - socket_transmitter.send:
          id: transmitter
          data: "Hello, World!"
```

## `wmbus_common`

This component is a port of [wmbusmeters](https://wmbusmeters.org/) library to ESPHome. It provides common functionality for working with wM-Bus devices, including parsing and handling wM-Bus packets.

**Example configuration:**

```yaml
wmbus_common:
    drivers: all
    # or specify specific drivers
    drivers:
      - apator162
      - amiplus
```

`wmbusmeters` is included as a git subtree. To sync version from upstream repository, run:

```bash
./scripts/pull_wmbusmeters.py [GIT_REF]
```

## `wmbus_meter`

This component provides abstraction for Meter object for wM-Bus devices. Attaching instance to the `wmbus_radio` component allows to receive, decrypt and parse wM-Bus packets from the radio interface.

**Example configuration:**

```yaml
wmbus_meter:
  id: my_water_meter
  radio_id: radio_component
  meter_id: 12345678
  type: apator162
  key: "00000000000000000000000000000000"
  mode: T1
  on_telegram:
    - socket_transmitter.send:
        id: transmitter
        data: !lambda return meter.as_json();
    - wmbus_meter.send_telegram_with_mqtt:
        topic: test/topic
```

`mode` parameter is optional and allows to filter received packets by mode. It can be set to `T1` or `C1`. If not set, all packets will be processed.
`key` parameter is optional and allows to decrypt packets using AES-128-CBC encryption. It should be provided as hexadecimal or ASCII encoded string. If not set, packets will be processed as unencrypted.

Component provides `on_telegram` trigger that can be used to send data to a remote server or process it in any other way. It can be used to send data to MQTT broker, HTTP server, or any other service. `meter` variable is available in the following lambdas.
Additionally, `wmbus_meter.send_telegram_with_mqtt` action can be used to send JSON-encoded meter data to MQTT broker. It requires `mqtt` component to be configured in ESPHome.

On the `wmbus_meter` platform, you can use the following sensors to provide data to Home Assistant/MQTT:

- **sensor**: as abstraction for numerical values from the meter, such as consumption, volume, etc.

  ```yaml
  sensor:
    - platform: wmbus_meter
      id: my_water_meter_consumption
      parent_id: my_water_meter
      name: "Water Meter Total Consumption"
      field: total_m3
  ```

  By default, unit of measurement is picked from the field name, but you can override it by specifying `unit_of_measurement` parameter. Especially, if you want to do this, ESPHome's `multiply` filter may be useful to change numerical value to the desired unit.

- **text_sensor**
  ```yaml
  text_sensor:
    - platform: wmbus_meter
      id: some_sensor
      parent_id: my_water_meter
      name: My Water Meter Timestamp
      field: timestamp
  ```

For both `sensor` and `text_sensor`, all config from generic [Sensor](https://esphome.io/components/sensor/index.html) and [Text Sensor](https://esphome.io/components/text_sensor/index.html) components is available, so you can use filters, icons, etc.

## `wmbus_radio`
This component provides a radio interface for 868MHz transceivers to receive wM-Bus packets. It requires the ESPHome `spi` component.

### Supported radios

## Required pins:
- **Semtech SX1276 (RFM95/RFM96)**
  - `irq_pin` connects to DIO1.
  - `reset_pin` connects to the module reset pin.
- **TI CC1101**
  - `gdo0_pin` connects to GDO0 (edge-triggered interrupt).

### Minimal ESPHome configuration
```yaml
spi:
  id: spi_bus
  clk_pin: GPIO1
  miso_pin: GPIO2
  mosi_pin: GPIO3

wmbus_radio:
  id: radio_component
  radio_type: SX1276   # or CC1101
  reset_pin: GPIO4 # set for SX radios
  irq_pin: GPIO5 # set for SX radios
  # gdo0_pin: GPIO5 # set for CC1101 radio
  on_frame:
    - wmbus_radio.send_frame_with_socket:
        id: transmitter
        format: rtlwmbus
```

### Notes per radio
- **SX1276**
  - Uses DIO1 for RX FIFO signalling.
  - Keep antenna matching for 868MHz; use 3.3 V supply only.
- **CC1101**
  - Uses GDO0 interrupts with burst reads from RX FIFO.
  - Ensure GDO0 is routed to an interrupt-capable MCU pin.

### Consuming frames
Use the `on_frame` trigger to forward packets via `socket_transmitter`, MQTT, or custom automations. Frames are delivered in radio-independent format so handlers work for both SX1276 and CC1101.
