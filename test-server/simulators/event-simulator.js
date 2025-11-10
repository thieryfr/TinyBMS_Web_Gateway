/**
 * EventSimulator
 * Gère un bus d'événements simulé pour refléter l'activité du système.
 */

const MAX_EVENTS = 200;

const EVENT_TYPES = [
  { type: 'SYSTEM', severity: 'info', message: 'System check completed' },
  { type: 'BATTERY', severity: 'info', message: 'Battery telemetry updated' },
  { type: 'COMM', severity: 'info', message: 'MQTT publish success' },
  { type: 'COMM', severity: 'warning', message: 'MQTT reconnect attempt' },
  { type: 'UART', severity: 'info', message: 'UART frame decoded' },
  { type: 'CAN', severity: 'info', message: 'CAN bus frame processed' },
  { type: 'THERMAL', severity: 'warning', message: 'Temperature rising in zone 2' },
  { type: 'BALANCING', severity: 'info', message: 'Cell balancing active' },
  { type: 'SECURITY', severity: 'warning', message: 'New web login session' },
  { type: 'SYSTEM', severity: 'error', message: 'Watchdog reset avoided' },
];

function randomEventTemplate() {
  return EVENT_TYPES[Math.floor(Math.random() * EVENT_TYPES.length)];
}

export class EventSimulator {
  constructor() {
    this.events = [];
  }

  /**
   * Ajoute un événement au journal.
   */
  addEvent(type, message, severity = 'info', metadata = {}) {
    const event = {
      id: `${Date.now()}-${Math.random().toString(16).slice(2, 6)}`,
      type,
      severity,
      message,
      timestamp_ms: Date.now(),
      metadata,
    };

    this.events.push(event);

    if (this.events.length > MAX_EVENTS) {
      this.events.shift();
    }

    return event;
  }

  /**
   * Génère un événement aléatoire pré-déterminé.
   */
  generateRandomEvent(context = {}) {
    const template = randomEventTemplate();
    return this.addEvent(template.type, template.message, template.severity, context);
  }

  /**
   * Retourne les derniers événements, triés du plus récent au plus ancien.
   */
  getEvents(limit = 50) {
    return this.events.slice(-limit).reverse();
  }
}

export default EventSimulator;
