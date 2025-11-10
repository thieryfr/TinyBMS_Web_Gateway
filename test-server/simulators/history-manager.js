/**
 * HistoryManager
 * Gère la génération, la conservation et l'export des données historiques
 * pour le serveur de test TinyBMS-GW.
 */

const DEFAULT_MAX_ENTRIES = 512;
const DEFAULT_INTERVAL_MS = 60_000; // 1 minute

export class HistoryManager {
  constructor(options = {}) {
    this.maxEntries = options.maxEntries || DEFAULT_MAX_ENTRIES;
    this.intervalMs = options.intervalMs || DEFAULT_INTERVAL_MS;
    this.entries = [];
    this.archives = this.#generateArchiveDescriptors();
    this.stateFile = options.stateFile || null;

    this.#generateInitialHistory();
  }

  /**
   * Génère un historique initial représentant plusieurs heures
   * d'utilisation de la batterie avec un cycle charge/décharge.
   */
  #generateInitialHistory() {
    const now = Date.now();

    for (let i = this.maxEntries - 1; i >= 0; i -= 1) {
      const timestamp = now - i * this.intervalMs;
      const cyclePosition = ((this.maxEntries - i) % 600) / 600; // cycle de 10 minutes

      const sample = this.#generateSampleFromCycle(cyclePosition, timestamp);
      this.entries.push(sample);
    }
  }

  /**
   * Crée un échantillon en fonction de la position dans le cycle de 10 minutes.
   */
  #generateSampleFromCycle(cyclePosition, timestamp) {
    let soc;
    let current;

    if (cyclePosition < 0.3) {
      // Décharge progressive
      const progress = cyclePosition / 0.3;
      soc = 90 - progress * 60;
      current = -10 - progress * 15 + Math.sin(progress * Math.PI) * -5;
    } else if (cyclePosition < 0.4) {
      // Phase idle
      const noise = (Math.random() - 0.5) * 0.5;
      soc = 30 + noise * 2;
      current = noise;
    } else if (cyclePosition < 0.9) {
      // Charge CC/CV
      const progress = (cyclePosition - 0.4) / 0.5;
      soc = 30 + progress * 65;
      current = 25 - progress * 18;
    } else {
      // Équilibrage
      const progress = (cyclePosition - 0.9) / 0.1;
      soc = 95 + progress * 5;
      current = 2 - progress * 1.5;
    }

    const packVoltage = 48 + (soc / 100) * 10 + (Math.random() - 0.5) * 0.2;
    const soh = 97.5 - (Math.random() * 0.5);
    const avgTemp = 25 + Math.abs(current) * 0.25 + Math.sin(cyclePosition * Math.PI * 2) * 2;

    return {
      timestamp_ms: timestamp,
      pack_voltage_v: parseFloat(packVoltage.toFixed(2)),
      pack_current_a: parseFloat(current.toFixed(2)),
      state_of_charge_pct: parseFloat(Math.max(0, Math.min(100, soc)).toFixed(1)),
      state_of_health_pct: parseFloat(soh.toFixed(1)),
      average_temperature_c: parseFloat(avgTemp.toFixed(1)),
      power_w: parseFloat((packVoltage * current).toFixed(1)),
    };
  }

  /**
   * Ajoute un échantillon basé sur les données de télémétrie courantes.
   */
  addEntry(telemetrySnapshot) {
    if (!telemetrySnapshot) {
      return;
    }

    const entry = {
      timestamp_ms: telemetrySnapshot.timestamp_ms || Date.now(),
      pack_voltage_v: telemetrySnapshot.pack_voltage_v,
      pack_current_a: telemetrySnapshot.pack_current_a,
      state_of_charge_pct: telemetrySnapshot.state_of_charge_pct,
      state_of_health_pct: telemetrySnapshot.state_of_health_pct,
      average_temperature_c: telemetrySnapshot.average_temperature_c,
      power_w: telemetrySnapshot.power_w,
    };

    this.entries.push(entry);

    if (this.entries.length > this.maxEntries) {
      this.entries.shift();
    }
  }

  /**
   * Retourne les données historiques avec limite et offset.
   */
  getHistory(limit = this.maxEntries, offset = 0) {
    const safeOffset = Math.max(0, offset);
    const safeLimit = Math.min(limit, this.maxEntries);

    const end = this.entries.length - safeOffset;
    const start = Math.max(0, end - safeLimit);
    const slice = this.entries.slice(start, end);

    return {
      count: slice.length,
      capacity: this.maxEntries,
      interval_ms: this.intervalMs,
      entries: slice,
    };
  }

  /**
   * Retourne le nombre d'entrées enregistrées.
   */
  getEntryCount() {
    return this.entries.length;
  }

  /**
   * Estime l'espace mémoire utilisé.
   */
  getStorageUsed() {
    const estimatedEntrySize = 160; // estimation grossière (octets)
    return this.entries.length * estimatedEntrySize;
  }

  /**
   * Fournit la liste des fichiers d'archive fictifs.
   */
  getArchiveFiles() {
    return { files: this.archives };
  }

  /**
   * Génère un CSV téléchargeable pour les derniers échantillons.
   */
  generateCSV(limit = 100) {
    const slice = this.entries.slice(-limit);
    const header = 'Timestamp,Pack Voltage (V),Pack Current (A),SOC (%),SOH (%),Temperature (°C),Power (W)';
    const rows = slice.map((entry) => {
      const date = new Date(entry.timestamp_ms).toISOString();
      return [
        date,
        entry.pack_voltage_v,
        entry.pack_current_a,
        entry.state_of_charge_pct,
        entry.state_of_health_pct,
        entry.average_temperature_c,
        entry.power_w,
      ].join(',');
    });

    return [header, ...rows].join('\n');
  }

  /**
   * Efface l'historique courant.
   */
  clearHistory() {
    this.entries = [];
  }

  /**
   * Sauvegarde l'état si un fichier de persistance est défini.
   */
  saveState() {
    // Pour le serveur de simulation, nous ne persistons pas sur disque par défaut.
    return false;
  }

  /**
   * Fournit la dernière entrée enregistrée.
   */
  getLastEntry() {
    return this.entries[this.entries.length - 1] || null;
  }

  /**
   * Génère des métadonnées d'archive fictives.
   */
  #generateArchiveDescriptors() {
    const today = new Date();
    const files = [];

    for (let i = 1; i <= 7; i += 1) {
      const date = new Date(today.getTime() - i * 86_400_000);
      const filename = `history_${date.toISOString().split('T')[0]}.csv`;
      files.push({
        filename,
        size_bytes: 128_000 + Math.floor(Math.random() * 8_000),
        timestamp_ms: date.getTime(),
        entry_count: 1_440,
      });
    }

    return files;
  }
}

export default HistoryManager;
