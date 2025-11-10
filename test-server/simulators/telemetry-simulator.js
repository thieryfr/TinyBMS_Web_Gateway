/**
 * Simulateur de télémétrie batterie avancé
 * Génère des données réalistes pour une batterie 16S LiFePO4
 */

export class TelemetrySimulator {
  constructor() {
    // Configuration de la batterie
    this.config = {
      cells: 16,
      chemistry: 'LiFePO4',
      nominalVoltagePerCell: 3.2,
      maxVoltagePerCell: 3.65,
      minVoltagePerCell: 2.5,
      capacity: 100, // Ah
      maxChargeCurrent: 50, // A
      maxDischargeCurrent: 100, // A
    };

    // État initial
    this.state = {
      soc: 75.5, // State of Charge %
      soh: 98.5, // State of Health %
      cycleCount: 127,
      energyCharged: 0,
      energyDischarged: 0,
    };

    // Variables de simulation
    this.simulationTime = 0;
    this.phase = 'idle'; // 'charging', 'discharging', 'idle', 'balancing'
    this.phaseTimer = 0;
    
    // Données des cellules
    this.cells = this.initializeCells();
    
    // Températures
    this.temperatures = this.initializeTemperatures();
    
    // Courant et puissance
    this.current = 0;
    this.power = 0;
    
    // Balancing
    this.balancingActive = false;
    this.cellsBalancing = new Array(this.config.cells).fill(false);
    
    // Alarmes
    this.alarms = {
      overvoltage: false,
      undervoltage: false,
      overcurrent: false,
      overtemperature: false,
      cellImbalance: false,
    };
    
    // Profils de charge/décharge
    this.profiles = {
      charge: this.generateChargeProfile(),
      discharge: this.generateDischargeProfile(),
    };
  }

  /**
   * Initialise les cellules avec des variations réalistes
   */
  initializeCells() {
    const cells = [];
    const baseVoltage = 3.2 + (this.state.soc / 100) * 0.4;
    
    for (let i = 0; i < this.config.cells; i++) {
      cells.push({
        voltage: baseVoltage + (Math.random() - 0.5) * 0.05,
        resistance: 0.002 + Math.random() * 0.001, // 2-3 mΩ
        temperature: 25 + (Math.random() - 0.5) * 5,
        capacity: this.config.capacity * (0.98 + Math.random() * 0.04),
      });
    }
    
    return cells;
  }

  /**
   * Initialise les capteurs de température
   */
  initializeTemperatures() {
    return {
      bms: 30 + Math.random() * 5,
      battery: [
        25 + Math.random() * 3,
        25 + Math.random() * 3,
        25 + Math.random() * 3,
        25 + Math.random() * 3,
      ],
      ambient: 22 + Math.random() * 2,
    };
  }

  /**
   * Génère un profil de charge CC-CV réaliste
   */
  generateChargeProfile() {
    return {
      cc_current: this.config.maxChargeCurrent * 0.5, // 0.5C
      cv_voltage: this.config.maxVoltagePerCell * this.config.cells,
      cutoff_current: this.config.maxChargeCurrent * 0.05, // 5% du max
      efficiency: 0.95,
    };
  }

  /**
   * Génère un profil de décharge réaliste
   */
  generateDischargeProfile() {
    return {
      constant_power: false,
      power_limit: 2000, // W
      current_limit: this.config.maxDischargeCurrent,
      cutoff_voltage: this.config.minVoltagePerCell * this.config.cells,
      efficiency: 0.97,
    };
  }

  /**
   * Met à jour l'état de la simulation
   */
  update() {
    this.simulationTime++;
    this.phaseTimer++;
    
    // Décider de la phase selon le temps de simulation
    this.updatePhase();
    
    // Simuler selon la phase active
    switch (this.phase) {
      case 'charging':
        this.simulateCharging();
        break;
      case 'discharging':
        this.simulateDischarging();
        break;
      case 'balancing':
        this.simulateBalancing();
        break;
      case 'idle':
      default:
        this.simulateIdle();
        break;
    }
    
    // Mettre à jour les températures
    this.updateTemperatures();
    
    // Vérifier les conditions d'alarme
    this.checkAlarms();
    
    // Mettre à jour les statistiques
    this.updateStatistics();
    
    return this.getCurrentData();
  }

  /**
   * Gestion des phases de simulation
   */
  updatePhase() {
    const cycleTime = 600; // 10 minutes de cycle complet
    const cyclePosition = (this.simulationTime % cycleTime) / cycleTime;
    
    if (cyclePosition < 0.3) {
      // 30% du temps : décharge
      if (this.phase !== 'discharging') {
        this.phase = 'discharging';
        this.phaseTimer = 0;
        console.log('[Simulator] Phase: DISCHARGING');
      }
    } else if (cyclePosition < 0.4) {
      // 10% du temps : idle
      if (this.phase !== 'idle') {
        this.phase = 'idle';
        this.phaseTimer = 0;
        console.log('[Simulator] Phase: IDLE');
      }
    } else if (cyclePosition < 0.9) {
      // 50% du temps : charge
      if (this.phase !== 'charging') {
        this.phase = 'charging';
        this.phaseTimer = 0;
        console.log('[Simulator] Phase: CHARGING');
      }
    } else {
      // 10% du temps : balancing
      if (this.phase !== 'balancing') {
        this.phase = 'balancing';
        this.phaseTimer = 0;
        console.log('[Simulator] Phase: BALANCING');
      }
    }
  }

  /**
   * Simule la charge CC-CV
   */
  simulateCharging() {
    const packVoltage = this.getPackVoltage();
    const targetVoltage = this.profiles.charge.cv_voltage;
    
    if (packVoltage < targetVoltage && this.state.soc < 95) {
      // Phase CC (Constant Current)
      this.current = this.profiles.charge.cc_current;
      
      // Réduction progressive du courant près de la fin
      if (this.state.soc > 85) {
        this.current *= (95 - this.state.soc) / 10;
      }
    } else if (this.state.soc < 100) {
      // Phase CV (Constant Voltage)
      this.current = this.profiles.charge.cc_current * Math.exp(-(this.state.soc - 95) / 2);
      this.current = Math.max(this.current, this.profiles.charge.cutoff_current);
    } else {
      // Charge complète
      this.current = 0;
      this.phase = 'idle';
    }
    
    // Ajouter du bruit
    this.current += (Math.random() - 0.5) * 0.5;
    
    // Mettre à jour le SOC
    const chargeRate = (this.current / this.config.capacity) / 36; // Pour 0.1s d'intervalle
    this.state.soc = Math.min(100, this.state.soc + chargeRate);
    
    // Mettre à jour les cellules
    this.updateCellVoltages(true);
  }

  /**
   * Simule la décharge
   */
  simulateDischarging() {
    if (this.state.soc > 10) {
      // Courant de décharge variable (simulation de charge variable)
      this.current = -(5 + Math.random() * 15 + Math.sin(this.simulationTime / 10) * 5);
      
      // Pics occasionnels
      if (Math.random() < 0.05) {
        this.current *= 2;
      }
      
      // Limiter au maximum
      this.current = Math.max(this.current, -this.config.maxDischargeCurrent);
    } else {
      // Protection sous-tension
      this.current = 0;
      this.phase = 'idle';
    }
    
    // Mettre à jour le SOC
    const dischargeRate = (Math.abs(this.current) / this.config.capacity) / 36;
    this.state.soc = Math.max(0, this.state.soc - dischargeRate);
    
    // Mettre à jour les cellules
    this.updateCellVoltages(false);
  }

  /**
   * Simule l'équilibrage des cellules
   */
  simulateBalancing() {
    this.current = 0;
    this.balancingActive = true;
    
    // Trouver les cellules à équilibrer
    const avgVoltage = this.cells.reduce((sum, cell) => sum + cell.voltage, 0) / this.cells.length;
    const threshold = 0.02; // 20mV
    
    this.cellsBalancing = this.cells.map(cell => 
      Math.abs(cell.voltage - avgVoltage) > threshold
    );
    
    // Équilibrer progressivement
    this.cells.forEach((cell, i) => {
      if (this.cellsBalancing[i]) {
        const correction = (avgVoltage - cell.voltage) * 0.01;
        cell.voltage += correction;
      }
    });
    
    // Vérifier si l'équilibrage est terminé
    const maxDiff = Math.max(...this.cells.map(cell => 
      Math.abs(cell.voltage - avgVoltage)
    ));
    
    if (maxDiff < 0.01) {
      this.balancingActive = false;
      this.phase = 'idle';
      console.log('[Simulator] Balancing complete');
    }
  }

  /**
   * Simule l'état idle
   */
  simulateIdle() {
    // Courant de repos très faible
    this.current = (Math.random() - 0.5) * 0.1;
    this.balancingActive = false;
    this.cellsBalancing.fill(false);
    
    // Auto-décharge très lente
    if (Math.random() < 0.01) {
      this.state.soc = Math.max(0, this.state.soc - 0.01);
    }
  }

  /**
   * Met à jour les tensions des cellules
   */
  updateCellVoltages(charging) {
    const socFactor = this.state.soc / 100;
    const baseVoltage = this.config.minVoltagePerCell + 
                       (this.config.nominalVoltagePerCell - this.config.minVoltagePerCell) * socFactor;
    
    this.cells.forEach((cell, i) => {
      // Tension de base + effet du courant (loi d'Ohm)
      const irDrop = this.current * cell.resistance * 0.001;
      let voltage = baseVoltage + irDrop;
      
      // Ajouter de la variation entre cellules
      voltage += (Math.random() - 0.5) * 0.02;
      
      // Dégradation progressive (vieillissement)
      voltage *= (1 - (this.state.cycleCount * 0.00001));
      
      // Limiter aux bornes
      voltage = Math.max(this.config.minVoltagePerCell, 
                Math.min(this.config.maxVoltagePerCell, voltage));
      
      cell.voltage = voltage;
    });
  }

  /**
   * Met à jour les températures
   */
  updateTemperatures() {
    const ambientTemp = 22 + Math.sin(this.simulationTime / 100) * 3;
    const powerDissipation = Math.abs(this.current * this.current * 0.01); // I²R
    
    // Température BMS
    this.temperatures.bms += (powerDissipation * 0.1 - (this.temperatures.bms - ambientTemp) * 0.01);
    
    // Températures batterie
    this.temperatures.battery = this.temperatures.battery.map(temp => {
      const newTemp = temp + (powerDissipation * 0.2 - (temp - ambientTemp) * 0.02);
      return Math.max(ambientTemp, Math.min(60, newTemp));
    });
    
    // Température ambiante
    this.temperatures.ambient = ambientTemp;
    
    // Mettre à jour les températures des cellules
    this.cells.forEach((cell, i) => {
      const zoneTemp = this.temperatures.battery[Math.floor(i / 4)];
      cell.temperature = zoneTemp + (Math.random() - 0.5) * 2;
    });
  }

  /**
   * Vérifie les conditions d'alarme
   */
  checkAlarms() {
    const packVoltage = this.getPackVoltage();
    const maxCellVoltage = Math.max(...this.cells.map(c => c.voltage));
    const minCellVoltage = Math.min(...this.cells.map(c => c.voltage));
    const maxTemp = Math.max(...this.temperatures.battery);
    
    this.alarms.overvoltage = maxCellVoltage > 3.6;
    this.alarms.undervoltage = minCellVoltage < 2.8;
    this.alarms.overcurrent = Math.abs(this.current) > this.config.maxDischargeCurrent * 0.9;
    this.alarms.overtemperature = maxTemp > 55;
    this.alarms.cellImbalance = (maxCellVoltage - minCellVoltage) > 0.05;
  }

  /**
   * Met à jour les statistiques
   */
  updateStatistics() {
    // Énergie
    const deltaTime = 1 / 3600; // 1 seconde en heures
    const power = this.getPackVoltage() * this.current;
    
    if (this.current > 0) {
      this.state.energyCharged += power * deltaTime;
    } else {
      this.state.energyDischarged += Math.abs(power) * deltaTime;
    }
    
    // Cycles (simplifiés)
    if (this.phase === 'charging' && this.state.soc > 95 && this.phaseTimer === 1) {
      this.state.cycleCount++;
      // Dégradation légère du SOH
      this.state.soh = Math.max(70, this.state.soh - 0.01);
    }
  }

  /**
   * Calcule la tension totale du pack
   */
  getPackVoltage() {
    return this.cells.reduce((sum, cell) => sum + cell.voltage, 0);
  }

  /**
   * Effectue un test de santé de la batterie
   */
  performHealthCheck() {
    const cellVoltages = this.cells.map(c => c.voltage);
    const avgVoltage = cellVoltages.reduce((a, b) => a + b) / cellVoltages.length;
    const voltageDeviation = Math.max(...cellVoltages.map(v => Math.abs(v - avgVoltage)));
    
    return {
      overall: this.state.soh > 80 ? 'GOOD' : this.state.soh > 60 ? 'FAIR' : 'POOR',
      soh_percent: this.state.soh,
      internal_resistance_mohm: this.cells.reduce((sum, c) => sum + c.resistance, 0) / this.cells.length * 1000,
      cell_balance: voltageDeviation < 0.03 ? 'GOOD' : voltageDeviation < 0.05 ? 'FAIR' : 'POOR',
      voltage_deviation_mv: voltageDeviation * 1000,
      capacity_fade_percent: 100 - this.state.soh,
      estimated_cycles_remaining: Math.max(0, 2000 - this.state.cycleCount)
    };
  }

  /**
   * Obtient les données actuelles formatées
   */
  getCurrentData() {
    const packVoltage = this.getPackVoltage();
    
    return {
      timestamp_ms: Date.now(),
      
      // Tensions
      pack_voltage_v: parseFloat(packVoltage.toFixed(2)),
      pack_current_a: parseFloat(this.current.toFixed(2)),
      power_w: parseFloat((packVoltage * this.current).toFixed(1)),
      
      // État de charge
      state_of_charge_pct: parseFloat(this.state.soc.toFixed(1)),
      state_of_health_pct: parseFloat(this.state.soh.toFixed(1)),
      
      // Cellules
      cell_count: this.config.cells,
      cell_voltage_mv: this.cells.map(c => Math.round(c.voltage * 1000)),
      cell_voltage_min_mv: Math.round(Math.min(...this.cells.map(c => c.voltage)) * 1000),
      cell_voltage_max_mv: Math.round(Math.max(...this.cells.map(c => c.voltage)) * 1000),
      cell_voltage_avg_mv: Math.round(this.cells.reduce((sum, c) => sum + c.voltage, 0) / this.cells.length * 1000),
      cell_delta_mv: Math.round((Math.max(...this.cells.map(c => c.voltage)) - 
                                  Math.min(...this.cells.map(c => c.voltage))) * 1000),
      
      // Températures
      temperature_bms_c: parseFloat(this.temperatures.bms.toFixed(1)),
      temperature_cells_c: this.temperatures.battery.map(t => parseFloat(t.toFixed(1))),
      temperature_ambient_c: parseFloat(this.temperatures.ambient.toFixed(1)),
      average_temperature_c: parseFloat(
        (this.temperatures.battery.reduce((a, b) => a + b) / this.temperatures.battery.length).toFixed(1)
      ),
      
      // Balancing
      balancing_active: this.balancingActive,
      cells_balancing_active: this.cellsBalancing,
      cells_balancing_count: this.cellsBalancing.filter(b => b).length,
      
      // Statistiques
      charge_cycles: this.state.cycleCount,
      energy_charged_kwh: parseFloat((this.state.energyCharged / 1000).toFixed(2)),
      energy_discharged_kwh: parseFloat((this.state.energyDischarged / 1000).toFixed(2)),
      
      // Protection
      charge_enabled: !this.alarms.overvoltage && !this.alarms.overtemperature,
      discharge_enabled: !this.alarms.undervoltage && !this.alarms.overtemperature,
      
      // État du système
      bms_status: this.phase.toUpperCase(),
      charging_state: this.phase === 'charging' ? 'CC_CV' : 
                     this.phase === 'discharging' ? 'DISCHARGING' : 'IDLE',
      
      // Capacité
      remaining_capacity_ah: parseFloat((this.config.capacity * this.state.soc / 100).toFixed(1)),
      full_capacity_ah: this.config.capacity,
      
      // Estimations
      time_to_empty_minutes: this.current < -1 ? 
        Math.round((this.state.soc / 100 * this.config.capacity) / Math.abs(this.current) * 60) : null,
      time_to_full_minutes: this.current > 1 ? 
        Math.round(((100 - this.state.soc) / 100 * this.config.capacity) / this.current * 60) : null
    };
  }
}
