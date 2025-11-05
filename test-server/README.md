# TinyBMS-GW Local Test Server

Serveur de test local pour le développement et le test de l'interface web TinyBMS-GW sans matériel ESP32.

## 🎯 Fonctionnalités

- ✅ **Serveur web complet** : Sert tous les fichiers statiques (HTML/CSS/JS)
- ✅ **API REST complète** : Tous les endpoints `/api/*` sont mockés
- ✅ **WebSockets temps réel** : Données de télémétrie mises à jour chaque seconde
- ✅ **Simulation de batterie** : Cycles charge/décharge réalistes
- ✅ **16 cellules** : Voltages individuels avec balancing
- ✅ **Historique** : 512 échantillons avec génération automatique
- ✅ **Configuration modifiable** : MQTT, WiFi, CAN, UART
- ✅ **Registres BMS** : Lecture/écriture des paramètres BMS
- ✅ **Hot reload** : Modifications web visibles immédiatement

## 📋 Prérequis

- **Node.js** version 14 ou supérieure
- **npm** (inclus avec Node.js)

### Installation de Node.js sur Mac

```bash
# Via Homebrew (recommandé)
brew install node

# Vérifier l'installation
node --version
npm --version
```

## 🚀 Installation

```bash
# Aller dans le dossier test-server
cd test-server

# Installer les dépendances
npm install
```

## ▶️ Démarrage

```bash
# Lancer le serveur
npm start
```

Vous devriez voir :

```
============================================================
  TinyBMS-GW Local Test Server
============================================================

  🌐 Web Interface:  http://localhost:3000
  📡 WebSocket:      ws://localhost:3000/ws
  📁 Web Directory:  /path/to/web

  Available Endpoints:
    GET  /api/status            - System status
    GET  /api/config            - Device config
    POST /api/config            - Update config
    GET  /api/mqtt/config       - MQTT config
    POST /api/mqtt/config       - Update MQTT
    GET  /api/mqtt/status       - MQTT status
    GET  /api/history           - History data
    GET  /api/history/files     - Archive files
    GET  /api/history/download  - Download CSV
    GET  /api/registers         - BMS registers
    POST /api/registers         - Update registers

  Press Ctrl+C to stop
============================================================
```

## 🌐 Accès à l'interface web

Ouvrir dans votre navigateur :

```
http://localhost:3000
```

## 📊 Données simulées

### Télémétrie batterie

- **Voltage pack** : 48-57V (16S LiFePO4)
- **Courant** : -50A à +50A (charge/décharge)
- **SOC** : 0-100% avec cycles réalistes
- **SOH** : ~98%
- **16 cellules** : 3.0-3.6V avec variations
- **Températures** : 15-45°C
- **Balancing** : Activé automatiquement si différence > 30mV
- **Alarmes/Warnings** : Selon les seuils

### Cycle de simulation

1. **Phase de décharge** (0-30% du temps) : SOC 90% → 20%, courant -5 à -8A
2. **Phase idle** (30-40%) : SOC stable ~20%, courant ~0A
3. **Phase de charge** (40-100%) : SOC 20% → 95%, courant 15A → 5A (taper)

Les données se mettent à jour automatiquement toutes les secondes via WebSocket.

### Historique

- **512 échantillons** en RAM (comme l'ESP32)
- **1 échantillon/minute** (~8.5 heures d'historique)
- **Génération automatique** : Nouvel échantillon ajouté chaque 60 secondes
- **Fichiers archivés** : 3 fichiers CSV mockés disponibles

## 🔧 Endpoints API

### Status et Télémétrie

```bash
# Obtenir le status complet du système
curl http://localhost:3000/api/status

# Retourne:
# - device: info système (nom, hostname, uptime, version)
# - battery: données temps réel (voltage, courant, SOC, cellules)
# - wifi: status connexion WiFi
# - mqtt: status connexion MQTT
```

### Configuration

```bash
# Lire la configuration
curl http://localhost:3000/api/config

# Modifier la configuration
curl -X POST http://localhost:3000/api/config \
  -H "Content-Type: application/json" \
  -d '{"device": {"name": "My TinyBMS"}}'
```

### MQTT

```bash
# Configuration MQTT
curl http://localhost:3000/api/mqtt/config

# Status MQTT
curl http://localhost:3000/api/mqtt/status

# Modifier MQTT
curl -X POST http://localhost:3000/api/mqtt/config \
  -H "Content-Type: application/json" \
  -d '{"broker_uri": "mqtt://test.mosquitto.org:1883"}'
```

### Historique

```bash
# Obtenir l'historique (défaut: 512 échantillons)
curl http://localhost:3000/api/history

# Limiter à 100 échantillons
curl http://localhost:3000/api/history?limit=100

# Lister les fichiers archivés
curl http://localhost:3000/api/history/files

# Télécharger en CSV
curl http://localhost:3000/api/history/download -o history.csv
```

### Registres BMS

```bash
# Lire tous les registres
curl http://localhost:3000/api/registers

# Modifier un registre
curl -X POST http://localhost:3000/api/registers \
  -H "Content-Type: application/json" \
  -d '{"registers": [{"address": 0, "value": 3600}]}'
```

## 🔌 WebSocket

Le serveur WebSocket est accessible à `ws://localhost:3000/ws`

### Types de messages

1. **telemetry** : Données batterie temps réel (1Hz)
   ```json
   {
     "type": "telemetry",
     "data": {
       "pack_voltage_v": 51.2,
       "pack_current_a": -5.3,
       "state_of_charge_pct": 75.5,
       "cell_voltage_mv": [3200, 3205, 3198, ...],
       ...
     }
   }
   ```

2. **notification** : Événements système (périodique)
   ```json
   {
     "type": "notification",
     "data": {
       "type": "info",
       "message": "System running normally"
     },
     "timestamp": 1234567890
   }
   ```

3. **config_updated** : Configuration modifiée
4. **mqtt_config_updated** : Config MQTT modifiée
5. **registers_updated** : Registres BMS modifiés

### Test WebSocket

```javascript
// Dans la console du navigateur
const ws = new WebSocket('ws://localhost:3000/ws');

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  console.log(msg.type, msg.data);
};
```

## 🛠️ Développement

### Structure des fichiers

```
test-server/
├── server.js              # Serveur principal Express + WebSocket
├── package.json           # Dépendances Node.js
├── mock-data/
│   ├── telemetry.js      # Générateur données batterie
│   ├── config.js         # Configuration mockée
│   ├── history.js        # Historique mocké
│   └── registers.js      # Registres BMS mockés
└── README.md             # Documentation
```

### Modifier les données simulées

#### Changer les valeurs initiales

Éditer `mock-data/telemetry.js` :

```javascript
constructor() {
  this.soc = 75.5;            // SOC initial
  this.packVoltage = 51.2;    // Voltage initial
  this.packCurrent = -5.3;    // Courant initial
  // ...
}
```

#### Ajuster la vitesse de simulation

```javascript
this.simulationSpeed = 10.0;  // 10x plus rapide
```

#### Forcer un état spécifique

```javascript
this.isCharging = true;       // Toujours en charge
this.packCurrent = 20.0;      // Courant de charge fixe
```

### Ajouter des endpoints

Dans `server.js` :

```javascript
app.get('/api/custom', (req, res) => {
  res.json({ custom: 'data' });
});
```

### Logs

Le serveur affiche :
- Requêtes HTTP reçues
- Connexions/déconnexions WebSocket
- Ajouts d'historique
- Événements simulés

## 🧪 Tests

### Tester tous les endpoints

```bash
# Script de test rapide
for endpoint in status config mqtt/config mqtt/status history registers; do
  echo "Testing /api/$endpoint"
  curl -s http://localhost:3000/api/$endpoint | jq .
done
```

### Tester les modifications

```bash
# Modifier la config
curl -X POST http://localhost:3000/api/config \
  -H "Content-Type: application/json" \
  -d '{"device": {"name": "TEST"}}' | jq .

# Vérifier
curl http://localhost:3000/api/config | jq .device.name
```

### Tester les registres

```bash
# Lire les registres
curl http://localhost:3000/api/registers | jq .

# Modifier un registre (overvoltage protection)
curl -X POST http://localhost:3000/api/registers \
  -H "Content-Type: application/json" \
  -d '{"registers": [{"address": 0, "value": 3700}]}' | jq .
```

## 🔄 Hot Reload

Modifications automatiquement détectées :

1. **Fichiers web** (`../web/`) : Rechargez simplement le navigateur (F5)
2. **Serveur Node.js** : Arrêtez (Ctrl+C) et relancez `npm start`

Pour le hot reload automatique du serveur :

```bash
# Installer nodemon (déjà dans devDependencies)
npm install

# Lancer avec hot reload
npm run dev
```

## 🐛 Dépannage

### Port 3000 déjà utilisé

Changer le port dans `server.js` :

```javascript
const PORT = 8080;  // ou autre port libre
```

### WebSocket ne se connecte pas

Vérifier la console du navigateur. L'URL WebSocket doit être `ws://localhost:3000/ws`

### Données ne se mettent pas à jour

Vérifier que le WebSocket est connecté :

```javascript
// Console navigateur
console.log(ws.readyState); // 1 = OPEN
```

## 📝 Notes

- **Aucune persistance** : Les données sont en RAM, redémarrer efface tout
- **Mono-utilisateur** : Pas de gestion multi-utilisateurs
- **Pas de sécurité** : Serveur de test uniquement, ne pas exposer sur internet
- **CORS activé** : Permet les requêtes depuis n'importe quelle origine

## 🚀 Prochaines étapes

Après avoir testé localement :

1. Modifier l'interface web dans `../web/`
2. Tester les changements en temps réel
3. Compiler et flasher sur ESP32 quand prêt
4. L'interface fonctionnera de la même façon sur ESP32

## 📞 Support

Pour toute question sur TinyBMS-GW, voir le README principal du projet.

---

**Bon développement ! 🎉**
