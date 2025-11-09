# TinyBMS-GW Web Interface - Testing Guide

Guide complet pour l'exécution et la maintenance des tests automatisés de l'interface web.

[![Tests](https://img.shields.io/badge/tests-passing-success.svg)]()
[![Coverage](https://img.shields.io/badge/coverage-70%25-yellow.svg)]()

---

## 📋 Table des Matières

- [Aperçu](#aperçu)
- [Configuration](#configuration)
- [Exécution des Tests](#exécution-des-tests)
- [Structure des Tests](#structure-des-tests)
- [Écrire des Tests](#écrire-des-tests)
- [Coverage](#coverage)
- [CI/CD](#cicd)
- [Troubleshooting](#troubleshooting)

---

## 🎯 Aperçu

### Framework de Test

- **Framework:** Jest 29.7.0
- **Environment:** jsdom (simulation navigateur)
- **Modules:** ES6+ avec support `import/export`
- **Coverage:** Seuils de 70% (branches, functions, lines, statements)

### Modules Testés

1. **logger.js** - Système de logging structuré
2. **alerts.js** - Gestion des alertes
3. **validation.js** - Validation d'entrées et sécurité
4. **api.test.js** - Utilitaires API et fetch
5. **websocket.test.js** - Gestion WebSocket

### Philosophie

- **Tests unitaires** pour chaque fonction critique
- **Mocks** pour isoler le code testé
- **Coverage minimum** de 70%
- **Fast feedback** - tests rapides (<5s total)

---

## ⚙️ Configuration

### Installation

```bash
# Depuis le dossier web/
cd web

# Installer dépendances
npm install

# Vérifier installation
npm test -- --version
```

### Fichiers de Configuration

**package.json** - Scripts de test:
```json
{
  "scripts": {
    "test": "node --experimental-vm-modules node_modules/jest/bin/jest.js",
    "test:watch": "... --watch",
    "test:coverage": "... --coverage",
    "test:verbose": "... --verbose"
  }
}
```

**jest.config.js** - Configuration Jest:
```javascript
export default {
  testEnvironment: 'jsdom',
  transform: {},
  moduleNameMapper: {
    '^(\\.{1,2}/.*)\\.js$': '$1'
  },
  coverageThreshold: {
    global: {
      branches: 70,
      functions: 70,
      lines: 70,
      statements: 70
    }
  }
};
```

**test/setup.js** - Mocks globaux:
- localStorage
- sessionStorage
- matchMedia (pour theme.js)
- IntersectionObserver (pour lazy.js)
- fetch API

---

## 🚀 Exécution des Tests

### Commandes de Base

```bash
# Exécuter tous les tests
npm test

# Mode watch (re-exécution automatique)
npm run test:watch

# Avec verbose output
npm run test:verbose

# Coverage report
npm run test:coverage
```

### Exécution Sélective

```bash
# Tester un seul fichier
npm test -- logger.test.js

# Tester par pattern
npm test -- --testNamePattern="Logger"

# Tester fichiers modifiés (git)
npm test -- --onlyChanged
```

### Options Utiles

```bash
# Bail on first failure
npm test -- --bail

# Force exit après tests
npm test -- --forceExit

# No coverage collection (plus rapide)
npm test -- --no-coverage

# Update snapshots
npm test -- --updateSnapshot
```

---

## 📁 Structure des Tests

### Organisation

```
web/
├── test/
│   ├── setup.js              # Configuration globale
│   ├── logger.test.js        # Tests logger
│   ├── alerts.test.js        # Tests alerts
│   ├── validation.test.js    # Tests validation/sécurité
│   ├── api.test.js           # Tests API utilities
│   └── websocket.test.js     # Tests WebSocket
│
├── jest.config.js
└── package.json
```

### Conventions de Nommage

- **Fichiers:** `{module}.test.js`
- **Describe blocks:** Nom du module ou fonctionnalité
- **Test names:** Description claire du comportement testé

**Exemple:**
```javascript
describe('Logger Module', () => {
  describe('Configuration', () => {
    test('should configure log level by string', () => {
      // ...
    });
  });
});
```

---

## ✍️ Écrire des Tests

### Template de Base

```javascript
/**
 * @file mymodule.test.js
 * @brief Unit tests for mymodule
 */

import { describe, test, expect, beforeEach, jest } from '@jest/globals';
import { myFunction } from '../src/js/mymodule.js';

describe('My Module', () => {
  beforeEach(() => {
    // Setup avant chaque test
  });

  test('should do something', () => {
    const result = myFunction('input');
    expect(result).toBe('expected');
  });
});
```

### Matchers Courants

```javascript
// Equality
expect(value).toBe(expected);           // Strict equality (===)
expect(value).toEqual(expected);        // Deep equality
expect(value).not.toBe(expected);       // Negation

// Truthiness
expect(value).toBeTruthy();
expect(value).toBeFalsy();
expect(value).toBeDefined();
expect(value).toBeNull();
expect(value).toBeUndefined();

// Numbers
expect(value).toBeGreaterThan(3);
expect(value).toBeGreaterThanOrEqual(3);
expect(value).toBeLessThan(5);
expect(value).toBeCloseTo(0.3, 2);      // Float comparison

// Strings
expect(string).toContain('substring');
expect(string).toMatch(/pattern/);

// Arrays
expect(array).toContain(item);
expect(array.length).toBe(3);

// Exceptions
expect(() => fn()).toThrow();
expect(() => fn()).toThrow('error message');

// Async
await expect(promise).resolves.toBe(value);
await expect(promise).rejects.toThrow();
```

### Mocking

**Mock Functions:**
```javascript
const mockFn = jest.fn();
mockFn('arg1', 'arg2');

expect(mockFn).toHaveBeenCalled();
expect(mockFn).toHaveBeenCalledWith('arg1', 'arg2');
expect(mockFn).toHaveBeenCalledTimes(1);
```

**Mock Return Values:**
```javascript
const mockFn = jest.fn()
  .mockReturnValue('default')
  .mockReturnValueOnce('first call')
  .mockReturnValueOnce('second call');
```

**Mock Implementations:**
```javascript
const mockFn = jest.fn((a, b) => a + b);
```

**Mock Modules:**
```javascript
jest.mock('../src/js/mymodule.js', () => ({
  myFunction: jest.fn(() => 'mocked')
}));
```

### Tests Asynchrones

**Async/Await:**
```javascript
test('async test', async () => {
  const result = await fetchData();
  expect(result).toBe('data');
});
```

**Promises:**
```javascript
test('promise test', () => {
  return fetchData().then(data => {
    expect(data).toBe('data');
  });
});
```

**Rejections:**
```javascript
test('should reject', async () => {
  await expect(fetchData()).rejects.toThrow('Error');
});
```

### Tests DOM

```javascript
beforeEach(() => {
  document.body.innerHTML = `
    <div id="container"></div>
  `;
});

test('should update DOM', () => {
  const container = document.getElementById('container');
  container.innerHTML = '<p>Hello</p>';

  expect(container.innerHTML).toContain('Hello');
});
```

---

## 📊 Coverage

### Générer Report

```bash
npm run test:coverage
```

### Visualiser Coverage

```bash
# Ouvrir rapport HTML
open coverage/index.html  # macOS
xdg-open coverage/index.html  # Linux
start coverage/index.html  # Windows
```

### Seuils de Coverage

Définis dans `jest.config.js`:

```javascript
coverageThreshold: {
  global: {
    branches: 70,    // 70% des branches
    functions: 70,   // 70% des fonctions
    lines: 70,       // 70% des lignes
    statements: 70   // 70% des statements
  }
}
```

### Exclure Fichiers

```javascript
collectCoverageFrom: [
  'src/js/**/*.js',
  '!src/js/lib/**',        // Exclure libraries externes
  '!src/js/tabler.min.js', // Exclure fichiers minifiés
  '!**/node_modules/**'
]
```

---

## 🔄 CI/CD

### GitHub Actions

**Exemple workflow (.github/workflows/tests.yml):**

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Setup Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'

      - name: Install dependencies
        run: |
          cd web
          npm install

      - name: Run tests
        run: |
          cd web
          npm test -- --coverage

      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          directory: ./web/coverage
```

### Pre-commit Hook

**Exemple .git/hooks/pre-commit:**

```bash
#!/bin/bash
cd web
npm test
if [ $? -ne 0 ]; then
  echo "Tests failed. Commit aborted."
  exit 1
fi
```

---

## 🛠️ Troubleshooting

### Problème: "Cannot use import statement"

**Cause:** Node.js ne reconnaît pas les modules ES6

**Solution:**
```bash
# Utiliser le flag --experimental-vm-modules
node --experimental-vm-modules node_modules/jest/bin/jest.js
```

### Problème: "localStorage is not defined"

**Cause:** jsdom n'implémente pas localStorage par défaut

**Solution:** Vérifier `test/setup.js`:
```javascript
global.localStorage = {
  getItem: jest.fn(),
  setItem: jest.fn(),
  removeItem: jest.fn(),
  clear: jest.fn()
};
```

### Problème: "fetch is not defined"

**Cause:** fetch n'existe pas dans Node.js

**Solution:** Mock dans setup.js ou test:
```javascript
global.fetch = jest.fn();
```

### Problème: Tests très lents

**Solutions:**
```bash
# 1. Désactiver coverage
npm test -- --no-coverage

# 2. Limiter workers
npm test -- --maxWorkers=2

# 3. Exécuter en parallèle
npm test -- --runInBand
```

### Problème: "Out of memory"

**Solution:**
```bash
# Augmenter heap size
NODE_OPTIONS="--max-old-space-size=4096" npm test
```

### Problème: Faux positifs/négatifs

**Checklist:**
- [ ] Mocks correctement configurés?
- [ ] `beforeEach/afterEach` nettoient l'état?
- [ ] Pas de dépendances entre tests?
- [ ] Tests asynchrones avec `async/await`?

---

## 📝 Best Practices

### 1. Tests Isolés

```javascript
// ❌ Mauvais - État partagé
let counter = 0;
test('increment', () => {
  counter++;
  expect(counter).toBe(1);
});

// ✅ Bon - État local
test('increment', () => {
  let counter = 0;
  counter++;
  expect(counter).toBe(1);
});
```

### 2. Noms Descriptifs

```javascript
// ❌ Mauvais
test('logger works', () => { ... });

// ✅ Bon
test('should log debug message when level is DEBUG', () => { ... });
```

### 3. Arrange-Act-Assert

```javascript
test('should validate IPv4 address', () => {
  // Arrange
  const validIP = '192.168.1.1';

  // Act
  const result = isValidIPv4(validIP);

  // Assert
  expect(result).toBe(true);
});
```

### 4. Un Concept par Test

```javascript
// ❌ Mauvais - Teste trop de choses
test('logger', () => {
  configure({ level: 'DEBUG' });
  debug('message');
  expect(getHistory().length).toBe(1);

  clearHistory();
  expect(getHistory().length).toBe(0);

  // ...
});

// ✅ Bon - Tests séparés
test('should log debug message', () => { ... });
test('should clear history', () => { ... });
```

### 5. Tests de Sécurité

```javascript
// Toujours tester les cas limites
describe('XSS Prevention', () => {
  test('should escape script tags', () => {
    const malicious = '<script>alert("XSS")</script>';
    const escaped = escapeHtml(malicious);
    expect(escaped).not.toContain('<script>');
  });

  test('should handle null input', () => {
    expect(escapeHtml(null)).toBe('');
  });
});
```

---

## 📚 Ressources

- [Jest Documentation](https://jestjs.io/docs/getting-started)
- [Testing Library](https://testing-library.com/)
- [JavaScript Testing Best Practices](https://github.com/goldbergyoni/javascript-testing-best-practices)
- [ES6 Modules in Jest](https://jestjs.io/docs/ecmascript-modules)

---

## 🔗 Liens Utiles

- [README.md](README.md) - Documentation principale
- [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) - Guide d'intégration
- [API_REFERENCE.md](API_REFERENCE.md) - Référence API

---

**Auteur:** TinyBMS Team
**Dernière mise à jour:** 2025-01-09
**Version:** 1.0.0
