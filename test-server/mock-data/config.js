/**
 * Mock configuration data for TinyBMS-GW
 */

let config = {
  device: {
    name: "TinyBMS-GW-TEST",
    hostname: "tinybms-test"
  },
  wifi: {
    sta: {
      ssid: "TestNetwork",
      password: "********"
    },
    ap: {
      ssid: "TinyBMS-AP",
      password: "tinybms123",
      channel: 6
    }
  },
  uart: {
    tx_pin: 17,
    rx_pin: 16,
    baud_rate: 9600
  },
  can: {
    tx_pin: 5,
    rx_pin: 4,
    enabled: true,
    identity: {
      manufacturer: "TinyBMS",
      model: "Gateway",
      serial: "TEST-001"
    }
  },
  mqtt: {
    enabled: true,
    broker_uri: "mqtt://test.mosquitto.org:1883",
    username: "",
    password: "",
    client_id: "tinybms-gw-test",
    topic_prefix: "tinybms",
    qos: 1,
    retain: false,
    publish_interval_ms: 5000
  }
};

module.exports = {
  /**
   * Get current configuration
   */
  getConfig() {
    return JSON.parse(JSON.stringify(config)); // Deep copy
  },

  /**
   * Update configuration
   */
  updateConfig(updates) {
    config = { ...config, ...updates };
    return this.getConfig();
  },

  /**
   * Get MQTT configuration
   */
  getMqttConfig() {
    return JSON.parse(JSON.stringify(config.mqtt));
  },

  /**
   * Update MQTT configuration
   */
  updateMqttConfig(updates) {
    config.mqtt = { ...config.mqtt, ...updates };
    return this.getMqttConfig();
  },

  /**
   * Get MQTT status
   */
  getMqttStatus() {
    return {
      enabled: config.mqtt.enabled,
      connected: config.mqtt.enabled && config.mqtt.broker_uri.length > 0,
      broker_uri: config.mqtt.broker_uri,
      client_id: config.mqtt.client_id,
      topic_prefix: config.mqtt.topic_prefix,
      messages_sent: 1234,
      messages_failed: 2,
      last_error: "",
      uptime_seconds: 86400
    };
  }
};
