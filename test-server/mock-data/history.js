/**
 * Mock history data for TinyBMS-GW
 * Generates realistic historical battery data
 */

const MAX_HISTORY_ENTRIES = 512;

class HistoryGenerator {
  constructor() {
    this.entries = [];
    this.generateInitialHistory();
  }

  /**
   * Generate initial historical data (last 512 samples, 1 per minute = ~8.5 hours)
   */
  generateInitialHistory() {
    const now = Date.now();
    const intervalMs = 60000; // 1 minute between samples

    for (let i = MAX_HISTORY_ENTRIES - 1; i >= 0; i--) {
      const timestamp = now - (i * intervalMs);

      // Simulate a realistic charge/discharge cycle
      const cyclePosition = (MAX_HISTORY_ENTRIES - i) / MAX_HISTORY_ENTRIES;
      let soc, voltage, current;

      if (cyclePosition < 0.3) {
        // Discharging phase (0-30%)
        soc = 90 - (cyclePosition / 0.3) * 70;
        voltage = 48.0 + (soc / 100) * 8.8;
        current = -(5 + Math.random() * 3);
      } else if (cyclePosition < 0.4) {
        // Idle phase (30-40%)
        soc = 20 + Math.random() * 2;
        voltage = 48.0 + (soc / 100) * 8.8;
        current = (Math.random() - 0.5) * 0.5;
      } else {
        // Charging phase (40-100%)
        const chargeProgress = (cyclePosition - 0.4) / 0.6;
        soc = 20 + chargeProgress * 75;
        voltage = 48.0 + (soc / 100) * 8.8;
        current = 15 - (chargeProgress * 10); // Taper charging
      }

      // Temperature varies with current
      const temperature = 22 + Math.abs(current) * 0.3 + Math.sin(cyclePosition * Math.PI) * 3;

      // SOH slowly degrades over time (but very slowly)
      const soh = 98.5 - (cyclePosition * 0.3);

      this.entries.push({
        timestamp_ms: timestamp,
        pack_voltage_v: parseFloat(voltage.toFixed(2)),
        pack_current_a: parseFloat(current.toFixed(2)),
        state_of_charge_pct: parseFloat(soc.toFixed(1)),
        state_of_health_pct: parseFloat(soh.toFixed(1)),
        average_temperature_c: parseFloat(temperature.toFixed(1)),
        power_w: parseFloat((voltage * current).toFixed(1))
      });
    }
  }

  /**
   * Add new history entry
   */
  addEntry(telemetrySnapshot) {
    const entry = {
      timestamp_ms: telemetrySnapshot.timestamp_ms,
      pack_voltage_v: telemetrySnapshot.pack_voltage_v,
      pack_current_a: telemetrySnapshot.pack_current_a,
      state_of_charge_pct: telemetrySnapshot.state_of_charge_pct,
      state_of_health_pct: telemetrySnapshot.state_of_health_pct,
      average_temperature_c: telemetrySnapshot.average_temperature_c,
      power_w: telemetrySnapshot.power_w
    };

    this.entries.push(entry);

    // Keep only last MAX_HISTORY_ENTRIES
    if (this.entries.length > MAX_HISTORY_ENTRIES) {
      this.entries.shift();
    }
  }

  /**
   * Get history data (with optional limit)
   */
  getHistory(limit = MAX_HISTORY_ENTRIES) {
    const count = Math.min(limit, this.entries.length);
    return {
      count: count,
      capacity: MAX_HISTORY_ENTRIES,
      interval_ms: 60000,
      entries: this.entries.slice(-count)
    };
  }

  /**
   * Get list of archived history files
   */
  getArchiveFiles() {
    // Simulate some archived files
    return {
      files: [
        {
          filename: "history_2024_01_15.csv",
          size_bytes: 125440,
          timestamp_ms: Date.now() - 86400000 * 7,
          entry_count: 1440
        },
        {
          filename: "history_2024_01_14.csv",
          size_bytes: 124800,
          timestamp_ms: Date.now() - 86400000 * 8,
          entry_count: 1440
        },
        {
          filename: "history_2024_01_13.csv",
          size_bytes: 123920,
          timestamp_ms: Date.now() - 86400000 * 9,
          entry_count: 1440
        }
      ]
    };
  }

  /**
   * Get archived history data
   */
  getArchiveData(filename, limit = 100) {
    // Return a subset of current history as mock archived data
    return {
      filename: filename,
      count: limit,
      entries: this.entries.slice(0, limit)
    };
  }

  /**
   * Generate CSV download content
   */
  generateCSV(filename) {
    const entries = this.entries.slice(0, 100); // First 100 for mock

    let csv = "Timestamp,Pack Voltage (V),Pack Current (A),SOC (%),SOH (%),Temperature (°C),Power (W)\n";

    entries.forEach(entry => {
      const date = new Date(entry.timestamp_ms).toISOString();
      csv += `${date},${entry.pack_voltage_v},${entry.pack_current_a},${entry.state_of_charge_pct},${entry.state_of_health_pct},${entry.average_temperature_c},${entry.power_w}\n`;
    });

    return csv;
  }
}

// Export singleton instance
module.exports = new HistoryGenerator();
