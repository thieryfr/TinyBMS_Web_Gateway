# Système d'Alertes TinyBMS-GW - Guide d'Intégration

Ce document décrit toutes les modifications nécessaires pour intégrer le nouveau système d'alertes dans TinyBMS-GW.

## 📋 Vue d'ensemble

Le système d'alertes ajoute les fonctionnalités suivantes :
- **Surveillance des seuils configurables** (température, tension, courant, SOC, imbalance)
- **Gestion des événements TinyBMS** (Faults, Warnings, Info)
- **Suivi du statut TinyBMS** (Registre 50 : Charging, Discharging, Idle, Fault, etc.)
- **Historique des alertes** (buffer circulaire de 100 entrées)
- **Notifications temps réel** via MQTT et WebSocket
- **Interface web complète** pour configuration et visualisation

## 🔧 Fichiers créés

### Backend (C)
- `main/alert_manager/alert_manager.h` - Interface publique du module
- `main/alert_manager/alert_manager.c` - Implémentation complète
- `main/alert_manager/CMakeLists.txt` - Configuration CMake
- `main/web_server/web_server_alerts.h` - Handlers HTTP/WebSocket pour alertes
- `main/web_server/web_server_alerts.c` - Implémentation des endpoints API

### Frontend (HTML/JS)
À créer dans `spiffs_image/` :
- Onglet Alertes (intégration dans index.html)
- Page de configuration des alertes
- Badge de notification dans le header
- Affichage du status TinyBMS dans le bandeau

## 🔨 Modifications à apporter

### 1. Intégrer alert_manager dans web_server.c

#### Étape 1.1 : Ajouter les includes
```c
// Au début de web_server.c, ajouter :
#include "alert_manager.h"
#include "web_server_alerts.h"
```

#### Étape 1.2 : Déclarer la liste de clients WebSocket pour alertes
```c
// Avec les autres listes de clients WS (ligne ~62)
static ws_client_t *s_alert_clients = NULL;
```

#### Étape 1.3 : Enregistrer les endpoints API

Dans la fonction `web_server_init()`, après les autres `httpd_register_uri_handler()` :

```c
// Alert API endpoints
const httpd_uri_t api_alerts_config_get = {
    .uri = "/api/alerts/config",
    .method = HTTP_GET,
    .handler = web_server_api_alerts_config_get_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_config_get);

const httpd_uri_t api_alerts_config_post = {
    .uri = "/api/alerts/config",
    .method = HTTP_POST,
    .handler = web_server_api_alerts_config_post_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_config_post);

const httpd_uri_t api_alerts_active = {
    .uri = "/api/alerts/active",
    .method = HTTP_GET,
    .handler = web_server_api_alerts_active_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_active);

const httpd_uri_t api_alerts_history = {
    .uri = "/api/alerts/history",
    .method = HTTP_GET,
    .handler = web_server_api_alerts_history_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_history);

const httpd_uri_t api_alerts_ack = {
    .uri = "/api/alerts/acknowledge",
    .method = HTTP_POST,
    .handler = web_server_api_alerts_acknowledge_all_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_ack);

const httpd_uri_t api_alerts_ack_id = {
    .uri = "/api/alerts/acknowledge/*",
    .method = HTTP_POST,
    .handler = web_server_api_alerts_acknowledge_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_ack_id);

const httpd_uri_t api_alerts_stats = {
    .uri = "/api/alerts/statistics",
    .method = HTTP_GET,
    .handler = web_server_api_alerts_statistics_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_stats);

const httpd_uri_t api_alerts_clear = {
    .uri = "/api/alerts/history",
    .method = HTTP_DELETE,
    .handler = web_server_api_alerts_clear_history_handler,
    .user_ctx = NULL,
};
httpd_register_uri_handler(s_httpd, &api_alerts_clear);

// WebSocket endpoint for alerts
const httpd_uri_t ws_alerts = {
    .uri = "/ws/alerts",
    .method = HTTP_GET,
    .handler = web_server_ws_alerts_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true,
};
httpd_register_uri_handler(s_httpd, &ws_alerts);
```

#### Étape 1.4 : Initialiser le module alert_manager

Dans `web_server_init()`, après `event_bus_init()` :

```c
// Initialize alert manager
alert_manager_init();
alert_manager_set_event_publisher(event_bus_get_publish_hook());
```

### 2. Mettre à jour CMakeLists.txt

#### Dans main/CMakeLists.txt

Ajouter `alert_manager` à la liste des sous-répertoires :

```cmake
set(app_sources
    "alert_manager/alert_manager.c"
    # ... autres sources
)
```

Ou si le projet utilise des sous-composants :

```cmake
set(COMPONENT_REQUIRES
    # ... autres composants
    alert_manager
)
```

#### Dans main/web_server/CMakeLists.txt

Ajouter `web_server_alerts.c` aux sources :

```cmake
idf_component_register(
    SRCS
        "web_server.c"
        "web_server_alerts.c"  # <-- Ajouter cette ligne
    # ... reste du fichier
)
```

Et ajouter `alert_manager` aux dépendances :

```cmake
REQUIRES
    # ... autres requires
    alert_manager
```

### 3. Étendre MQTT Gateway (optionnel mais recommandé)

Dans `main/mqtt_gateway/mqtt_gateway.c` :

#### Étape 3.1 : S'abonner aux événements d'alertes

```c
#include "alert_manager.h"

// Dans mqtt_gateway_init(), ajouter un callback pour les événements d'alertes
static void mqtt_alert_event_callback(const event_bus_event_t *event, void *context)
{
    if (event->id == EVENT_ID_ALERT_TRIGGERED) {
        const alert_entry_t *alert = (const alert_entry_t *)event->payload;

        // Publier l'alerte sur MQTT
        char topic[128];
        snprintf(topic, sizeof(topic), "tinybms/alerts");

        char payload[256];
        snprintf(payload, sizeof(payload),
                 "{\"id\":%lu,\"type\":%d,\"severity\":%d,\"message\":\"%s\"}",
                 alert->alert_id, alert->type, alert->severity, alert->message);

        mqtt_client_publish(topic, payload, strlen(payload), 0, false);
    }
}
```

### 4. Interface Web - Ajout de l'onglet Alertes

#### Modifications dans `spiffs_image/index.html`

##### 4.1 : Ajouter l'onglet dans la navigation

Dans la section `<ul class="nav nav-tabs">` :

```html
<li class="nav-item">
    <a class="nav-link" data-bs-toggle="tab" href="#tab-alerts" id="alerts-tab-link">
        <svg xmlns="http://www.w3.org/2000/svg" class="icon me-2" width="24" height="24" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" fill="none">
            <path stroke="none" d="M0 0h24v24H0z" fill="none"/>
            <path d="M12 9v2m0 4v.01M5 19h14a2 2 0 0 0 1.84 -2.75l-7.1 -12.25a2 2 0 0 0 -3.5 0l-7.1 12.25a2 2 0 0 0 1.75 2.75" />
        </svg>
        Alertes
        <span class="badge bg-danger ms-2" id="alert-count-badge" style="display:none;">0</span>
    </a>
</li>
```

##### 4.2 : Ajouter le contenu de l'onglet

Dans la section `<div class="tab-content">` :

```html
<div class="tab-pane fade" id="tab-alerts" role="tabpanel">
    <div class="row row-deck row-cards">
        <!-- Active Alerts Card -->
        <div class="col-12">
            <div class="card">
                <div class="card-header">
                    <h3 class="card-title">Alertes Actives</h3>
                    <div class="ms-auto">
                        <button class="btn btn-primary btn-sm" onclick="acknowledgeAllAlerts()">
                            Tout acquitter
                        </button>
                    </div>
                </div>
                <div class="card-body">
                    <div id="active-alerts-container">
                        <div class="text-muted text-center py-4">Aucune alerte active</div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Alert History Card -->
        <div class="col-12">
            <div class="card">
                <div class="card-header">
                    <h3 class="card-title">Historique des Alertes</h3>
                    <div class="ms-auto">
                        <button class="btn btn-outline-secondary btn-sm" onclick="clearAlertHistory()">
                            Effacer l'historique
                        </button>
                    </div>
                </div>
                <div class="card-body">
                    <div id="alert-history-container">
                        <div class="text-muted text-center py-4">Aucun historique</div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Alert Statistics Card -->
        <div class="col-12">
            <div class="card">
                <div class="card-header">
                    <h3 class="card-title">Statistiques</h3>
                </div>
                <div class="card-body">
                    <div class="row">
                        <div class="col-md-3">
                            <div class="text-center">
                                <div class="h1 m-0" id="stat-total-alerts">0</div>
                                <div class="text-muted">Total</div>
                            </div>
                        </div>
                        <div class="col-md-3">
                            <div class="text-center">
                                <div class="h1 m-0 text-danger" id="stat-critical">0</div>
                                <div class="text-muted">Critiques</div>
                            </div>
                        </div>
                        <div class="col-md-3">
                            <div class="text-center">
                                <div class="h1 m-0 text-warning" id="stat-warnings">0</div>
                                <div class="text-muted">Avertissements</div>
                            </div>
                        </div>
                        <div class="col-md-3">
                            <div class="text-center">
                                <div class="h1 m-0 text-info" id="stat-info">0</div>
                                <div class="text-muted">Informations</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>
```

##### 4.3 : Ajouter le JavaScript pour les alertes

Créer un fichier `spiffs_image/js/alerts.js` :

```javascript
// Alert Management JavaScript Module
let alertsWebSocket = null;

// Connect to alerts WebSocket
function connectAlertsWebSocket() {
    const wsUrl = `ws://${window.location.hostname}/ws/alerts`;
    alertsWebSocket = new WebSocket(wsUrl);

    alertsWebSocket.onopen = () => {
        console.log('Alerts WebSocket connected');
    };

    alertsWebSocket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === 'alerts') {
            // Handle real-time alert notification
            refreshActiveAlerts();
            updateAlertBadge();
        }
    };

    alertsWebSocket.onerror = (error) => {
        console.error('Alerts WebSocket error:', error);
    };

    alertsWebSocket.onclose = () => {
        console.log('Alerts WebSocket closed, reconnecting...');
        setTimeout(connectAlertsWebSocket, 5000);
    };
}

// Fetch and display active alerts
async function refreshActiveAlerts() {
    try {
        const response = await fetch('/api/alerts/active');
        const alerts = await response.json();

        const container = document.getElementById('active-alerts-container');

        if (!alerts || alerts.length === 0) {
            container.innerHTML = '<div class="text-muted text-center py-4">Aucune alerte active</div>';
            return;
        }

        container.innerHTML = alerts.map(alert => `
            <div class="alert alert-${getSeverityClass(alert.severity)} alert-dismissible fade show" role="alert">
                <div class="d-flex">
                    <div class="flex-grow-1">
                        <h4 class="alert-title">${getAlertTitle(alert.type)}</h4>
                        <div class="text-muted">${alert.message}</div>
                        <div class="small text-muted mt-1">
                            ${new Date(alert.timestamp_ms).toLocaleString('fr-FR')}
                        </div>
                    </div>
                    <div class="ms-3">
                        ${alert.status === 0 ? `
                            <button type="button" class="btn btn-sm btn-primary" onclick="acknowledgeAlert(${alert.id})">
                                Acquitter
                            </button>
                        ` : '<span class="badge bg-success">Acquitté</span>'}
                    </div>
                </div>
            </div>
        `).join('');

    } catch (error) {
        console.error('Failed to fetch active alerts:', error);
    }
}

// Fetch and display alert history
async function refreshAlertHistory() {
    try {
        const response = await fetch('/api/alerts/history?limit=50');
        const alerts = await response.json();

        const container = document.getElementById('alert-history-container');

        if (!alerts || alerts.length === 0) {
            container.innerHTML = '<div class="text-muted text-center py-4">Aucun historique</div>';
            return;
        }

        container.innerHTML = '<div class="list-group list-group-flush">' + alerts.map(alert => `
            <div class="list-group-item">
                <div class="row align-items-center">
                    <div class="col-auto">
                        <span class="badge bg-${getSeverityClass(alert.severity)}">${getSeverityLabel(alert.severity)}</span>
                    </div>
                    <div class="col text-truncate">
                        <div class="text-reset d-block">${alert.message}</div>
                        <div class="d-block text-muted text-truncate mt-n1">
                            ${new Date(alert.timestamp_ms).toLocaleString('fr-FR')}
                        </div>
                    </div>
                </div>
            </div>
        `).join('') + '</div>';

    } catch (error) {
        console.error('Failed to fetch alert history:', error);
    }
}

// Fetch and display alert statistics
async function refreshAlertStatistics() {
    try {
        const response = await fetch('/api/alerts/statistics');
        const stats = await response.json();

        document.getElementById('stat-total-alerts').textContent = stats.total_alerts || 0;
        document.getElementById('stat-critical').textContent = stats.critical_count || 0;
        document.getElementById('stat-warnings').textContent = stats.warning_count || 0;
        document.getElementById('stat-info').textContent = stats.info_count || 0;

    } catch (error) {
        console.error('Failed to fetch alert statistics:', error);
    }
}

// Update alert badge in navigation
async function updateAlertBadge() {
    try {
        const response = await fetch('/api/alerts/statistics');
        const stats = await response.json();

        const badge = document.getElementById('alert-count-badge');
        const activeCount = stats.active_alert_count || 0;

        if (activeCount > 0) {
            badge.textContent = activeCount;
            badge.style.display = 'inline-block';

            // Flash animation
            badge.classList.add('animate__animated', 'animate__flash');
            setTimeout(() => {
                badge.classList.remove('animate__animated', 'animate__flash');
            }, 1000);
        } else {
            badge.style.display = 'none';
        }

    } catch (error) {
        console.error('Failed to update alert badge:', error);
    }
}

// Acknowledge specific alert
async function acknowledgeAlert(alertId) {
    try {
        const response = await fetch(`/api/alerts/acknowledge/${alertId}`, {
            method: 'POST'
        });

        if (response.ok) {
            refreshActiveAlerts();
            updateAlertBadge();
        }
    } catch (error) {
        console.error('Failed to acknowledge alert:', error);
    }
}

// Acknowledge all alerts
async function acknowledgeAllAlerts() {
    try {
        const response = await fetch('/api/alerts/acknowledge', {
            method: 'POST'
        });

        if (response.ok) {
            refreshActiveAlerts();
            updateAlertBadge();
        }
    } catch (error) {
        console.error('Failed to acknowledge all alerts:', error);
    }
}

// Clear alert history
async function clearAlertHistory() {
    if (!confirm('Êtes-vous sûr de vouloir effacer tout l\'historique des alertes?')) {
        return;
    }

    try {
        const response = await fetch('/api/alerts/history', {
            method: 'DELETE'
        });

        if (response.ok) {
            refreshAlertHistory();
        }
    } catch (error) {
        console.error('Failed to clear alert history:', error);
    }
}

// Helper functions
function getSeverityClass(severity) {
    switch (severity) {
        case 2: return 'danger';  // Critical
        case 1: return 'warning'; // Warning
        case 0: return 'info';    // Info
        default: return 'secondary';
    }
}

function getSeverityLabel(severity) {
    switch (severity) {
        case 2: return 'CRITIQUE';
        case 1: return 'AVERTISSEMENT';
        case 0: return 'INFO';
        default: return 'INCONNU';
    }
}

function getAlertTitle(type) {
    // Map alert types to human-readable titles (peut être étendu)
    const titles = {
        1: 'Température Élevée',
        2: 'Température Basse',
        3: 'Tension Cellule Haute',
        4: 'Tension Cellule Basse',
        7: 'Courant de Décharge Élevé',
        9: 'SOC Faible',
        11: 'Déséquilibre Cellules Élevé',
        20: 'En Charge',
        21: 'Chargement Complet',
        22: 'En Décharge',
        24: 'Au Repos',
        25: 'Défaut Détecté',
    };
    return titles[type] || `Alerte Type ${type}`;
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    connectAlertsWebSocket();

    // Refresh every 10 seconds
    setInterval(() => {
        updateAlertBadge();
    }, 10000);

    // Refresh when tab is shown
    document.getElementById('alerts-tab-link')?.addEventListener('shown.bs.tab', () => {
        refreshActiveAlerts();
        refreshAlertHistory();
        refreshAlertStatistics();
    });
});
```

## 📄 Résumé des endpoints API

| Méthode | Endpoint | Description |
|---------|----------|-------------|
| GET | `/api/alerts/config` | Récupère la configuration des alertes |
| POST | `/api/alerts/config` | Met à jour la configuration des alertes |
| GET | `/api/alerts/active` | Liste des alertes actives |
| GET | `/api/alerts/history?limit=N` | Historique des alertes (limit optionnel) |
| POST | `/api/alerts/acknowledge/{id}` | Acquitter une alerte spécifique |
| POST | `/api/alerts/acknowledge` | Acquitter toutes les alertes |
| GET | `/api/alerts/statistics` | Statistiques des alertes |
| DELETE | `/api/alerts/history` | Effacer l'historique |
| WS | `/ws/alerts` | WebSocket pour notifications temps réel |

## ✅ Checklist d'intégration

- [ ] Copier les fichiers `alert_manager/*` dans `main/alert_manager/`
- [ ] Copier les fichiers `web_server_alerts.*` dans `main/web_server/`
- [ ] Modifier `main/web_server/web_server.c` (sections 1.1 à 1.4)
- [ ] Mettre à jour les CMakeLists.txt (section 2)
- [ ] Ajouter l'onglet Alertes dans `index.html` (section 4)
- [ ] Créer le fichier `js/alerts.js`
- [ ] Tester la compilation (`idf.py build`)
- [ ] Tester le fonctionnement sur l'ESP32
- [ ] Vérifier les alertes MQTT (optionnel)
- [ ] Valider l'interface web

## 🧪 Tests recommandés

1. **Test des seuils** : Configurer des seuils bas et vérifier que les alertes se déclenchent
2. **Test WebSocket** : Ouvrir l'onglet Alertes et vérifier la réception en temps réel
3. **Test d'acquittement** : Acquitter une alerte et vérifier qu'elle disparaît
4. **Test du statut TinyBMS** : Vérifier que les changements de statut (Reg:50) génèrent des alertes
5. **Test de persistance** : Redémarrer l'ESP32 et vérifier que la configuration est conservée

## 📞 Support

En cas de problème, vérifier :
- Les logs ESP32 (`idf.py monitor`)
- La console JavaScript du navigateur (F12)
- Les événements sur le bus d'événements

Bon développement ! 🚀
