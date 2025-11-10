# 🔋 TinyBMS-GW Enhanced Test Server v2.0

Serveur de test amélioré pour TinyBMS-GW avec simulation complète et réaliste de tous les modules du système de gestion de batterie.

## 🌟 Nouvelles Fonctionnalités

### ✨ Améliorations Principales

- **Simulation de batterie ultra-réaliste** : Cycles de charge CC-CV, décharge avec profils variables
- **16 cellules LiFePO4** : Avec variations individuelles, résistance interne, vieillissement
- **Gestion thermique avancée** : 4 zones de température, dissipation thermique simulée
- **Équilibrage intelligent** : Détection automatique du déséquilibre, simulation d'équilibrage actif
- **Alarmes et événements** : Système complet d'alarmes avec seuils configurables
- **Protocoles multiples** : UART, CAN, Modbus avec trames réalistes
- **Historique persistant** : Avec archivage automatique et export CSV/JSON
- **Diagnostics système** : Auto-tests, métriques de performance, analyse de santé

### 📊 Modules de Simulation

| Module | Description | Fréquence |
|--------|-------------|-----------|
| **Télémétrie** | Données batterie temps réel | 1 Hz |
| **UART** | Protocole TinyBMS/Modbus | 2 Hz |
| **CAN** | Protocole Victron/Pylontech | 10 Hz |
| **Événements** | Alarmes, notifications | Variable |
| **Historique** | Enregistrement données | 1/min |
| **Équilibrage** | Simulation balancing | Continue |

## 🚀 Installation Rapide

```bash
# Cloner ou télécharger les fichiers
cd enhanced-test-server

# Installer les dépendances
npm install

# Démarrer le serveur
npm start
```

## 📁 Structure du Projet

```
enhanced-test-server/
├── enhanced-test-server.js     # Serveur principal
├── package.json                 # Dépendances
├── .env                        # Configuration environnement (optionnel)
├── simulators/                 # Modules de simulation
│   ├── telemetry-simulator.js # Simulation batterie
│   ├── config-manager.js      # Gestion configuration
│   ├── history-manager.js     # Gestion historique
│   ├── registers-manager.js   # Registres BMS
│   ├── uart-simulator.js      # Simulation UART
│   ├── can-simulator.js       # Simulation CAN
│   ├── event-simulator.js     # Événements système
│   └── alarm-simulator.js     # Gestion alarmes
└── config.json                 # Configuration persistée (auto-généré)
```

## 🔧 Configuration

### Variables d'Environnement

Créez un fichier `.env` pour personnaliser :

```env
# Port du serveur
PORT=3000

# Répertoire de l'interface web
WEB_DIR=../web

# Persistance de la configuration
PERSIST_CONFIG=true

# Niveau de log
LOG_LEVEL=info

# Mode de simulation
SIMULATION_MODE=realistic  # 'realistic', 'test', 'demo'

# Vitesse de simulation
SIMULATION_SPEED=1.0       # 1.0 = temps réel, 2.0 = 2x plus rapide
```

### Configuration par API

Toute la configuration peut être modifiée via l'API REST :

```bash
# Obtenir la configuration complète
curl http://localhost:3000/api/config

# Modifier des paramètres
curl -X POST http://localhost:3000/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "battery": {
      "cells_series": 16,
      "capacity_ah": 100
    }
  }'
```

## 📡 Endpoints API

### 🔌 WebSocket Endpoints

| Endpoint | Description | Format |
|----------|-------------|--------|
| `/ws/telemetry` | Données batterie temps réel | JSON, 1Hz |
| `/ws/events` | Événements et alarmes | JSON, Variable |
| `/ws/uart` | Trames UART | HEX/JSON, 2Hz |
| `/ws/can` | Trames CAN | HEX/JSON, 10Hz |

### 🌐 REST API Endpoints

#### Status & Monitoring

```http
GET /api/status              # Status système complet
GET /api/diagnostics         # Diagnostics détaillés
GET /api/events?limit=100    # Derniers événements
GET /api/alarms              # Alarmes actives
```

#### Configuration

```http
GET  /api/config             # Configuration complète
POST /api/config             # Mise à jour config
GET  /api/config/export      # Exporter config
POST /api/config/import      # Importer config
POST /api/config/reset       # Réinitialiser
```

#### MQTT

```http
GET  /api/mqtt/config        # Config MQTT
POST /api/mqtt/config        # Mise à jour MQTT
GET  /api/mqtt/status        # Status connexion
```

#### Historique

```http
GET    /api/history?limit=100&offset=0  # Données historique
GET    /api/history/files               # Fichiers archive
GET    /api/history/download?format=csv # Télécharger
DELETE /api/history                     # Effacer
```

#### Registres BMS

```http
GET  /api/registers?category=protection  # Lire registres
POST /api/registers                      # Modifier
GET  /api/registers/export               # Exporter
POST /api/registers/import               # Importer
```

#### Communications

```http
GET /api/uart/status         # Status UART
GET /api/can/status          # Status CAN
```

#### Commandes

```http
POST /api/command            # Envoyer commande BMS
POST /api/alarms/acknowledge # Acquitter alarme
```

## 🔄 Cycles de Simulation

### Cycle de Batterie Complet (10 minutes)

1. **Phase Décharge** (0-30%) : 3 minutes
   - SOC : 90% → 20%
   - Courant : -5 à -15A (variable)
   - Pics occasionnels jusqu'à -30A

2. **Phase Idle** (30-40%) : 1 minute
   - SOC stable ~20%
   - Courant : ~0A (±0.1A)

3. **Phase Charge** (40-90%) : 5 minutes
   - SOC : 20% → 95%
   - Charge CC : 25A constant
   - Charge CV : Réduction progressive

4. **Phase Équilibrage** (90-100%) : 1 minute
   - Équilibrage actif si δV > 20mV
   - Convergence progressive

### Événements Aléatoires

- **Alarmes** : Génération selon seuils configurés
- **Événements système** : Toutes les 5-30 secondes
- **Variations température** : Cycle sinusoïdal + dissipation I²R
- **Perturbations** : Pics de courant, variations tension

## 📊 Exemples d'Utilisation

### Connexion WebSocket (JavaScript)

```javascript
// Connexion télémétrie
const ws = new WebSocket('ws://localhost:3000/ws/telemetry');

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  
  if (data.type === 'telemetry') {
    console.log('SOC:', data.data.state_of_charge_pct + '%');
    console.log('Voltage:', data.data.pack_voltage_v + 'V');
    console.log('Current:', data.data.pack_current_a + 'A');
    console.log('Power:', data.data.power_w + 'W');
    
    // Afficher l'état des cellules
    data.data.cell_voltage_mv.forEach((v, i) => {
      console.log(`Cell ${i+1}: ${v}mV`);
    });
  }
};

// Connexion événements
const wsEvents = new WebSocket('ws://localhost:3000/ws/events');

wsEvents.onmessage = (event) => {
  const data = JSON.parse(event.data);
  
  if (data.type === 'new_alarms') {
    console.warn('Nouvelles alarmes:', data.data);
  }
};
```

### Tests avec cURL

```bash
# Status complet du système
curl http://localhost:3000/api/status | jq .

# Modifier la capacité de la batterie
curl -X POST http://localhost:3000/api/config \
  -H "Content-Type: application/json" \
  -d '{"battery": {"capacity_ah": 200}}' | jq .

# Obtenir l'historique des 50 derniers échantillons
curl "http://localhost:3000/api/history?limit=50" | jq .

# Télécharger l'historique en CSV
curl "http://localhost:3000/api/history/download?format=csv" > history.csv

# Envoyer une commande au BMS
curl -X POST http://localhost:3000/api/command \
  -H "Content-Type: application/json" \
  -d '{"command": "reset_soc", "parameters": {"value": 100}}' | jq .

# Diagnostics complets
curl http://localhost:3000/api/diagnostics | jq .
```

### Python Example

```python
import requests
import websocket
import json
import threading

# REST API
def get_status():
    response = requests.get('http://localhost:3000/api/status')
    return response.json()

# WebSocket
def on_message(ws, message):
    data = json.loads(message)
    if data['type'] == 'telemetry':
        print(f"SOC: {data['data']['state_of_charge_pct']}%")

def start_websocket():
    ws = websocket.WebSocketApp("ws://localhost:3000/ws/telemetry",
                                on_message=on_message)
    ws.run_forever()

# Démarrer le monitoring
status = get_status()
print(f"Battery voltage: {status['battery']['pack_voltage_v']}V")

# WebSocket en thread séparé
ws_thread = threading.Thread(target=start_websocket)
ws_thread.start()
```

## 🔍 Monitoring et Debugging

### Logs Détaillés

Le serveur affiche des logs détaillés :

```
[2024-01-15T10:30:45.123Z] GET /api/status - IP: ::1
[History] Added sample #234
[Simulator] Phase: CHARGING
[WS] Telemetry client connected: a3b2c1
[Alarm] New alarm: CELL_IMBALANCE (delta: 52mV)
[ConfigManager] Configuration updated
```

### Mode Debug

Activez le mode debug pour plus de détails :

```env
LOG_LEVEL=debug
DEBUG_MODE=true
```

### Métriques de Performance

```bash
# Obtenir les métriques système
curl http://localhost:3000/api/diagnostics | jq .system

# Monitoring continu
watch -n 1 'curl -s http://localhost:3000/api/status | jq .device'
```

## 🎮 Modes de Simulation

### Mode Réaliste (par défaut)

- Cycles complets charge/décharge
- Variations naturelles
- Vieillissement progressif
- Événements aléatoires

### Mode Test

```env
SIMULATION_MODE=test
```

- Valeurs fixes configurables
- Pas d'événements aléatoires
- Idéal pour tests automatisés

### Mode Demo

```env
SIMULATION_MODE=demo
```

- Cycles accélérés
- Variations amplifiées
- Plus d'événements
- Parfait pour démonstrations

## 🛠️ Personnalisation Avancée

### Créer un Profil de Batterie Custom

```javascript
// Dans simulators/battery-profiles.js
export const customProfile = {
  chemistry: 'LTO',
  cells: 10,
  nominalVoltage: 2.3,
  maxVoltage: 2.8,
  minVoltage: 1.5,
  capacity: 50,
  maxChargeCurrent: 200,
  maxDischargeCurrent: 400
};
```

### Ajouter un Nouveau Protocole

```javascript
// Dans simulators/protocol-custom.js
export class CustomProtocol {
  generateFrame(telemetryData) {
    // Implémenter le protocole
    return {
      id: 0x100,
      data: Buffer.from([...]),
      timestamp: Date.now()
    };
  }
}
```

## 🐛 Dépannage

### Port déjà utilisé

```bash
# Changer le port
PORT=8080 npm start

# Ou tuer le processus
lsof -i :3000
kill -9 <PID>
```

### WebSocket ne se connecte pas

- Vérifier les logs du serveur
- Tester avec `wscat` :

```bash
npm install -g wscat
wscat -c ws://localhost:3000/ws/telemetry
```

### Performances lentes

- Réduire la fréquence de mise à jour
- Limiter le nombre de clients WebSocket
- Utiliser le mode test pour debug

## 📈 Roadmap

### v2.1 (Prévu)
- [ ] Support multi-batteries
- [ ] Simulation de défauts
- [ ] Interface graphique de contrôle
- [ ] Export Grafana/Prometheus

### v2.2 (Futur)
- [ ] Simulation réseau de batteries
- [ ] Machine learning pour prédictions
- [ ] Support Docker/Kubernetes
- [ ] API GraphQL

## 📝 License

MIT License - Voir [LICENSE](LICENSE)

## 🤝 Contribution

Les contributions sont bienvenues ! Voir [CONTRIBUTING.md](CONTRIBUTING.md)

## 📞 Support

- 📧 Email : support@tinybms.com
- 💬 Discord : [TinyBMS Community](https://discord.gg/tinybms)
- 📖 Documentation : [docs.tinybms.com](https://docs.tinybms.com)

---

**TinyBMS-GW Enhanced Test Server** - Développé avec ❤️ pour la communauté TinyBMS
