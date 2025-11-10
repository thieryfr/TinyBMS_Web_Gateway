/**
 * AlarmSimulator
 * Surveille les données de télémétrie pour déclencher des alarmes réalistes.
 */

const DEFAULT_THRESHOLDS = {
  voltage_high_warning_v: 3.55,
  voltage_high_critical_v: 3.65,
  voltage_low_warning_v: 2.9,
  voltage_low_critical_v: 2.5,
  current_high_warning_a: 40,
  current_high_critical_a: 50,
  temperature_high_warning_c: 45,
  temperature_high_critical_c: 55,
  soc_low_warning_pct: 20,
  soc_low_critical_pct: 10,
};

export class AlarmSimulator {
  constructor(configManager = null) {
    this.configManager = configManager;
    this.activeAlarms = new Map();
    this.history = [];
  }

  #getThresholds() {
    const configThresholds = this.configManager?.getConfig?.().alarms?.thresholds;
    return { ...DEFAULT_THRESHOLDS, ...(configThresholds || {}) };
  }

  #raiseAlarm(id, message, severity, category, details = {}) {
    if (this.activeAlarms.has(id)) {
      return { alarm: this.activeAlarms.get(id), created: false };
    }

    const alarm = {
      id,
      message,
      severity,
      category,
      timestamp_ms: Date.now(),
      acknowledged: false,
      details,
    };

    this.activeAlarms.set(id, alarm);
    this.history.push(alarm);
    return { alarm, created: true };
  }

  #clearAlarm(id) {
    if (!this.activeAlarms.has(id)) {
      return null;
    }

    const alarm = this.activeAlarms.get(id);
    alarm.cleared_at_ms = Date.now();
    this.activeAlarms.delete(id);
    return alarm;
  }

  checkAlarms(telemetry) {
    if (!telemetry) {
      return { active: [], new: [], cleared: [] };
    }

    const thresholds = this.#getThresholds();
    const newAlarms = [];
    const clearedAlarms = [];

    const maxCellMv = telemetry.cell_voltage_max_mv;
    const minCellMv = telemetry.cell_voltage_min_mv;
    const current = Math.abs(telemetry.pack_current_a);
    const temperature = Math.max(...(telemetry.temperature_cells_c || []));
    const soc = telemetry.state_of_charge_pct;

    if (maxCellMv / 1000 >= thresholds.voltage_high_critical_v) {
      const { alarm, created } = this.#raiseAlarm(
        'CELL_OVER_VOLTAGE',
        'Cell voltage critical high',
        'critical',
        'battery',
        { value_mv: maxCellMv },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else if (maxCellMv / 1000 >= thresholds.voltage_high_warning_v) {
      const { alarm, created } = this.#raiseAlarm(
        'CELL_HIGH_VOLTAGE',
        'Cell voltage high warning',
        'warning',
        'battery',
        { value_mv: maxCellMv },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else {
      const cleared = this.#clearAlarm('CELL_OVER_VOLTAGE') || this.#clearAlarm('CELL_HIGH_VOLTAGE');
      if (cleared) {
        clearedAlarms.push(cleared);
      }
    }

    if (minCellMv / 1000 <= thresholds.voltage_low_critical_v) {
      const { alarm, created } = this.#raiseAlarm(
        'CELL_UNDER_VOLTAGE',
        'Cell voltage critical low',
        'critical',
        'battery',
        { value_mv: minCellMv },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else if (minCellMv / 1000 <= thresholds.voltage_low_warning_v) {
      const { alarm, created } = this.#raiseAlarm(
        'CELL_LOW_VOLTAGE',
        'Cell voltage low warning',
        'warning',
        'battery',
        { value_mv: minCellMv },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else {
      const cleared = this.#clearAlarm('CELL_UNDER_VOLTAGE') || this.#clearAlarm('CELL_LOW_VOLTAGE');
      if (cleared) {
        clearedAlarms.push(cleared);
      }
    }

    if (current >= thresholds.current_high_critical_a) {
      const { alarm, created } = this.#raiseAlarm(
        'PACK_OVER_CURRENT',
        'Discharge current critical',
        'critical',
        'system',
        { value_a: current },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else if (current >= thresholds.current_high_warning_a) {
      const { alarm, created } = this.#raiseAlarm(
        'PACK_HIGH_CURRENT',
        'High discharge current',
        'warning',
        'system',
        { value_a: current },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else {
      const cleared = this.#clearAlarm('PACK_OVER_CURRENT') || this.#clearAlarm('PACK_HIGH_CURRENT');
      if (cleared) {
        clearedAlarms.push(cleared);
      }
    }

    if (temperature >= thresholds.temperature_high_critical_c) {
      const { alarm, created } = this.#raiseAlarm(
        'TEMP_CRITICAL',
        'Battery temperature critical',
        'critical',
        'thermal',
        { value_c: temperature },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else if (temperature >= thresholds.temperature_high_warning_c) {
      const { alarm, created } = this.#raiseAlarm(
        'TEMP_HIGH',
        'Battery temperature high',
        'warning',
        'thermal',
        { value_c: temperature },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else {
      const cleared = this.#clearAlarm('TEMP_CRITICAL') || this.#clearAlarm('TEMP_HIGH');
      if (cleared) {
        clearedAlarms.push(cleared);
      }
    }

    if (soc <= thresholds.soc_low_critical_pct) {
      const { alarm, created } = this.#raiseAlarm(
        'SOC_CRITICAL',
        'State of charge critical low',
        'critical',
        'battery',
        { value_pct: soc },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else if (soc <= thresholds.soc_low_warning_pct) {
      const { alarm, created } = this.#raiseAlarm(
        'SOC_LOW',
        'State of charge low',
        'warning',
        'battery',
        { value_pct: soc },
      );
      if (created) {
        newAlarms.push(alarm);
      }
    } else {
      const cleared = this.#clearAlarm('SOC_CRITICAL') || this.#clearAlarm('SOC_LOW');
      if (cleared) {
        clearedAlarms.push(cleared);
      }
    }

    return {
      active: Array.from(this.activeAlarms.values()),
      new: newAlarms,
      cleared: clearedAlarms,
    };
  }

  getActiveAlarms() {
    const active = Array.from(this.activeAlarms.values());
    return {
      active,
      battery: active.filter((alarm) => alarm.category === 'battery'),
      warnings: active.filter((alarm) => alarm.severity === 'warning'),
      critical: active.filter((alarm) => alarm.severity === 'critical'),
    };
  }

  getAllAlarms() {
    return {
      active: Array.from(this.activeAlarms.values()),
      history: this.history.slice(-200).reverse(),
    };
  }

  acknowledgeAlarm(alarmId) {
    const alarm = this.activeAlarms.get(alarmId);
    if (!alarm) {
      return { success: false, message: 'Alarm not found' };
    }

    alarm.acknowledged = true;
    alarm.acknowledged_at_ms = Date.now();
    return { success: true };
  }
}

export default AlarmSimulator;
