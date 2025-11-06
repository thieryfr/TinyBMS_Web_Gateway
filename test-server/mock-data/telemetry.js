/**
 * Mock telemetry data generator for TinyBMS-GW
 * Simulates realistic battery data with dynamic changes
 */

class TelemetryGenerator {
  constructor() {
    // Battery state
    this.soc = 75.5; // State of charge percentage
    this.soh = 98.2; // State of health percentage
    this.packVoltage = 51.2; // 16S LiFePO4 battery
    this.packCurrent = -5.3; // Negative = discharging
    this.cycleCount = 127;
    this.uptimeSeconds = 3600 * 24 * 7; // 1 week uptime

    // Energy counters (CAN ID 0x378)
    this.energyChargedWh = 125430; // Energy IN (charged) in Wh
    this.energyDischargedWh = 118650; // Energy OUT (discharged) in Wh

    // Cell voltages (16 cells, slightly different)
    this.cellVoltages = Array(16).fill(0).map((_, i) => 3.200 + (Math.random() * 0.050 - 0.025));

    // Temperatures
    this.avgTemperature = 23.5;
    this.mosfetTemperature = 28.2;
    this.auxTemperature = 22.8;
    this.minTemperature = 22.0;
    this.maxTemperature = 25.5;

    // Status flags
    this.balancingBits = 0b0000000000000000; // No cells balancing initially
    this.alarmBits = 0;
    this.warningBits = 0;

    // Simulation parameters
    this.isCharging = false;
    this.simulationSpeed = 1.0;
  }

  /**
   * Update telemetry values (simulate battery behavior)
   */
  update() {
    const deltaTime = 1.0 * this.simulationSpeed; // 1 second updates

    // Simulate charge/discharge cycle
    if (this.soc <= 20) {
      this.isCharging = true;
      this.packCurrent = 10.0 + Math.random() * 5; // Charging current
    } else if (this.soc >= 95) {
      this.isCharging = false;
      this.packCurrent = -(3.0 + Math.random() * 3); // Discharging current
    }

    // Update SOC based on current (simplified Ah counting)
    const batteryCapacityAh = 100.0;
    const socDelta = (this.packCurrent / batteryCapacityAh) * (deltaTime / 3600) * 100;
    this.soc = Math.max(0, Math.min(100, this.soc + socDelta));

    // Add small random variations to current
    this.packCurrent += (Math.random() - 0.5) * 0.5;

    // Update pack voltage based on SOC (simple linear model)
    const minVoltage = 48.0; // Empty voltage
    const maxVoltage = 56.8; // Full voltage
    this.packVoltage = minVoltage + (maxVoltage - minVoltage) * (this.soc / 100);
    this.packVoltage += (Math.random() - 0.5) * 0.2; // Add noise

    // Update cell voltages
    const avgCellVoltage = this.packVoltage / 16;
    this.cellVoltages = this.cellVoltages.map((v, i) => {
      // Drift towards average with small random changes
      const drift = (avgCellVoltage - v) * 0.1;
      const noise = (Math.random() - 0.5) * 0.005;
      return v + drift + noise;
    });

    // Cell balancing simulation (balance if difference > 30mV)
    const minCellV = Math.min(...this.cellVoltages);
    const maxCellV = Math.max(...this.cellVoltages);
    if ((maxCellV - minCellV) > 0.030 && this.soc > 90) {
      this.balancingBits = 0;
      this.cellVoltages.forEach((v, i) => {
        if (v > (minCellV + 0.020)) {
          this.balancingBits |= (1 << i);
        }
      });
    } else {
      this.balancingBits = 0;
    }

    // Update temperatures
    this.avgTemperature += (Math.random() - 0.5) * 0.3;
    this.avgTemperature = Math.max(15, Math.min(45, this.avgTemperature));

    this.mosfetTemperature = this.avgTemperature + Math.abs(this.packCurrent) * 0.5;
    this.auxTemperature = this.avgTemperature + (Math.random() - 0.5) * 2;
    this.minTemperature = Math.min(...[this.avgTemperature, this.auxTemperature]) - 1;
    this.maxTemperature = Math.max(...[this.avgTemperature, this.mosfetTemperature]) + 1;

    // Update alarms and warnings
    this.alarmBits = 0;
    this.warningBits = 0;

    // Low SOC warning
    if (this.soc < 20) this.warningBits |= (1 << 0);
    // High temperature warning
    if (this.avgTemperature > 40) this.warningBits |= (1 << 1);
    // Overcurrent warning
    if (Math.abs(this.packCurrent) > 50) this.warningBits |= (1 << 2);

    // Update energy counters based on power flow
    const powerW = this.packVoltage * this.packCurrent;
    const energyDeltaWh = (powerW * deltaTime) / 3600; // Wh = W * h
    if (this.packCurrent > 0) {
      // Charging
      this.energyChargedWh += Math.abs(energyDeltaWh);
    } else {
      // Discharging
      this.energyDischargedWh += Math.abs(energyDeltaWh);
    }

    // Update uptime
    this.uptimeSeconds += deltaTime;
  }

  /**
   * Get current telemetry snapshot in API format
   */
  getSnapshot() {
    const minCellMv = Math.min(...this.cellVoltages) * 1000;
    const maxCellMv = Math.max(...this.cellVoltages) * 1000;

    return {
      timestamp_ms: Date.now(),
      pack_voltage_v: parseFloat(this.packVoltage.toFixed(2)),
      pack_current_a: parseFloat(this.packCurrent.toFixed(2)),
      min_cell_mv: Math.round(minCellMv),
      max_cell_mv: Math.round(maxCellMv),
      state_of_charge_pct: parseFloat(this.soc.toFixed(1)),
      state_of_health_pct: parseFloat(this.soh.toFixed(1)),
      average_temperature_c: parseFloat(this.avgTemperature.toFixed(1)),
      mosfet_temperature_c: parseFloat(this.mosfetTemperature.toFixed(1)),
      auxiliary_temperature_c: parseFloat(this.auxTemperature.toFixed(1)),
      pack_temperature_min_c: parseFloat(this.minTemperature.toFixed(1)),
      pack_temperature_max_c: parseFloat(this.maxTemperature.toFixed(1)),
      balancing_bits: this.balancingBits,
      alarm_bits: this.alarmBits,
      warning_bits: this.warningBits,
      uptime_seconds: Math.floor(this.uptimeSeconds),
      cycle_count: this.cycleCount,
      battery_capacity_ah: 100.0,
      cell_voltage_mv: this.cellVoltages.map(v => Math.round(v * 1000)),
      cell_balancing: Array(16).fill(0).map((_, i) => (this.balancingBits & (1 << i)) ? 1 : 0),
      is_charging: this.isCharging,
      power_w: parseFloat((this.packVoltage * this.packCurrent).toFixed(1)),
      energy_charged_wh: Math.round(this.energyChargedWh),
      energy_discharged_wh: Math.round(this.energyDischargedWh)
    };
  }

  /**
   * Get status API response
   */
  getStatus() {
    const snapshot = this.getSnapshot();
    return {
      device: {
        name: "TinyBMS-GW-TEST",
        hostname: "tinybms-test",
        uptime_seconds: snapshot.uptime_seconds,
        firmware_version: "1.0.0-local-test",
        chip_model: "ESP32-S3",
        free_heap: 156432,
        min_free_heap: 142288
      },
      battery: snapshot,
      wifi: {
        connected: true,
        ssid: "TestNetwork",
        rssi: -45,
        ip: "192.168.1.100"
      },
      mqtt: {
        connected: true,
        broker: "mqtt://test.mosquitto.org:1883"
      }
    };
  }
}

// Export singleton instance
module.exports = new TelemetryGenerator();
