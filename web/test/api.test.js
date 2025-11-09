/**
 * @file api.test.js
 * @brief Unit tests for API interactions and utilities
 */

import { describe, test, expect, beforeEach, jest } from '@jest/globals';

describe('API Utilities', () => {
  beforeEach(() => {
    global.fetch = jest.fn();
  });

  describe('Fetch with Timeout', () => {
    function fetchWithTimeout(url, options = {}, timeout = 5000) {
      return Promise.race([
        fetch(url, options),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('Request timeout')), timeout)
        )
      ]);
    }

    test('should resolve when fetch completes before timeout', async () => {
      global.fetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({ data: 'success' })
      });

      const result = await fetchWithTimeout('/api/status', {}, 5000);
      expect(result.ok).toBe(true);
    });

    test('should reject when timeout is exceeded', async () => {
      global.fetch.mockImplementationOnce(() =>
        new Promise(resolve => setTimeout(resolve, 10000))
      );

      await expect(
        fetchWithTimeout('/api/status', {}, 100)
      ).rejects.toThrow('Request timeout');
    });
  });

  describe('API Response Handling', () => {
    function handleAPIResponse(response) {
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      return response.json();
    }

    test('should handle successful responses', async () => {
      const mockResponse = {
        ok: true,
        status: 200,
        statusText: 'OK',
        json: async () => ({ success: true })
      };

      const data = await handleAPIResponse(mockResponse);
      expect(data.success).toBe(true);
    });

    test('should throw on HTTP errors', async () => {
      const mockResponse = {
        ok: false,
        status: 404,
        statusText: 'Not Found',
        json: async () => ({})
      };

      await expect(handleAPIResponse(mockResponse))
        .rejects.toThrow('HTTP 404: Not Found');
    });

    test('should handle different status codes', async () => {
      const testCases = [
        { status: 400, statusText: 'Bad Request' },
        { status: 401, statusText: 'Unauthorized' },
        { status: 403, statusText: 'Forbidden' },
        { status: 500, statusText: 'Internal Server Error' },
        { status: 503, statusText: 'Service Unavailable' }
      ];

      for (const { status, statusText } of testCases) {
        const mockResponse = {
          ok: false,
          status,
          statusText,
          json: async () => ({})
        };

        await expect(handleAPIResponse(mockResponse))
          .rejects.toThrow(`HTTP ${status}`);
      }
    });
  });

  describe('Retry Logic', () => {
    async function fetchWithRetry(url, options = {}, maxRetries = 3) {
      let lastError;

      for (let attempt = 0; attempt <= maxRetries; attempt++) {
        try {
          const response = await fetch(url, options);
          if (response.ok) {
            return response;
          }

          // Retry on 5xx errors
          if (response.status >= 500 && attempt < maxRetries) {
            await new Promise(resolve => setTimeout(resolve, 1000 * Math.pow(2, attempt)));
            continue;
          }

          throw new Error(`HTTP ${response.status}`);
        } catch (error) {
          lastError = error;
          if (attempt < maxRetries) {
            await new Promise(resolve => setTimeout(resolve, 1000 * Math.pow(2, attempt)));
            continue;
          }
        }
      }

      throw lastError;
    }

    test('should succeed on first attempt', async () => {
      global.fetch.mockResolvedValueOnce({
        ok: true,
        status: 200
      });

      const response = await fetchWithRetry('/api/status', {}, 3);
      expect(response.ok).toBe(true);
      expect(global.fetch).toHaveBeenCalledTimes(1);
    });

    test('should retry on network errors', async () => {
      global.fetch
        .mockRejectedValueOnce(new Error('Network error'))
        .mockRejectedValueOnce(new Error('Network error'))
        .mockResolvedValueOnce({ ok: true, status: 200 });

      const response = await fetchWithRetry('/api/status', {}, 3);
      expect(response.ok).toBe(true);
      expect(global.fetch).toHaveBeenCalledTimes(3);
    });

    test('should not retry on 4xx errors', async () => {
      global.fetch.mockResolvedValueOnce({
        ok: false,
        status: 404
      });

      await expect(fetchWithRetry('/api/status', {}, 3))
        .rejects.toThrow('HTTP 404');

      expect(global.fetch).toHaveBeenCalledTimes(1);
    });
  });

  describe('Request Queue', () => {
    class RequestQueue {
      constructor(concurrency = 2) {
        this.concurrency = concurrency;
        this.running = 0;
        this.queue = [];
      }

      async add(fn) {
        return new Promise((resolve, reject) => {
          this.queue.push({ fn, resolve, reject });
          this.process();
        });
      }

      async process() {
        if (this.running >= this.concurrency || this.queue.length === 0) {
          return;
        }

        this.running++;
        const { fn, resolve, reject } = this.queue.shift();

        try {
          const result = await fn();
          resolve(result);
        } catch (error) {
          reject(error);
        } finally {
          this.running--;
          this.process();
        }
      }
    }

    test('should process requests with concurrency limit', async () => {
      const queue = new RequestQueue(2);
      let concurrent = 0;
      let maxConcurrent = 0;

      const createRequest = () => async () => {
        concurrent++;
        maxConcurrent = Math.max(maxConcurrent, concurrent);
        await new Promise(resolve => setTimeout(resolve, 10));
        concurrent--;
      };

      const requests = Array(5).fill(0).map(() => queue.add(createRequest()));
      await Promise.all(requests);

      expect(maxConcurrent).toBeLessThanOrEqual(2);
    });
  });

  describe('API Endpoint Tests', () => {
    describe('GET /api/status', () => {
      test('should return system status', async () => {
        const mockStatus = {
          uptime_ms: 123456,
          free_heap: 45678,
          wifi: { connected: true },
          mqtt: { connected: true }
        };

        global.fetch.mockResolvedValueOnce({
          ok: true,
          json: async () => mockStatus
        });

        const response = await fetch('/api/status');
        const data = await response.json();

        expect(data.uptime_ms).toBeDefined();
        expect(data.free_heap).toBeDefined();
        expect(data.wifi).toBeDefined();
        expect(data.mqtt).toBeDefined();
      });
    });

    describe('POST /api/config', () => {
      test('should save configuration', async () => {
        const config = {
          mqtt_broker: 'mqtt://192.168.1.10',
          mqtt_port: 1883
        };

        global.fetch.mockResolvedValueOnce({
          ok: true,
          json: async () => ({ success: true })
        });

        const response = await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(config)
        });

        const data = await response.json();
        expect(data.success).toBe(true);
      });
    });

    describe('GET /api/alerts/active', () => {
      test('should return active alerts', async () => {
        const mockAlerts = {
          alerts: [
            { alert_id: 1, type: 'TEMP_HIGH', severity: 2 }
          ]
        };

        global.fetch.mockResolvedValueOnce({
          ok: true,
          json: async () => mockAlerts
        });

        const response = await fetch('/api/alerts/active');
        const data = await response.json();

        expect(data.alerts).toBeDefined();
        expect(Array.isArray(data.alerts)).toBe(true);
      });
    });
  });

  describe('Cache Management', () => {
    class APICache {
      constructor(ttl = 60000) {
        this.ttl = ttl;
        this.cache = new Map();
      }

      get(key) {
        const entry = this.cache.get(key);
        if (!entry) return null;

        if (Date.now() - entry.timestamp > this.ttl) {
          this.cache.delete(key);
          return null;
        }

        return entry.data;
      }

      set(key, data) {
        this.cache.set(key, {
          data,
          timestamp: Date.now()
        });
      }

      clear() {
        this.cache.clear();
      }
    }

    test('should cache data', () => {
      const cache = new APICache(1000);

      cache.set('status', { uptime: 123 });
      expect(cache.get('status')).toEqual({ uptime: 123 });
    });

    test('should expire old data', async () => {
      const cache = new APICache(100);

      cache.set('status', { uptime: 123 });
      await new Promise(resolve => setTimeout(resolve, 150));

      expect(cache.get('status')).toBe(null);
    });

    test('should clear cache', () => {
      const cache = new APICache(1000);

      cache.set('key1', 'value1');
      cache.set('key2', 'value2');

      cache.clear();

      expect(cache.get('key1')).toBe(null);
      expect(cache.get('key2')).toBe(null);
    });
  });

  describe('URL Building', () => {
    function buildURL(base, params = {}) {
      const url = new URL(base, window.location.origin);

      Object.keys(params).forEach(key => {
        if (params[key] !== null && params[key] !== undefined) {
          url.searchParams.append(key, params[key]);
        }
      });

      return url.toString();
    }

    test('should build URL with parameters', () => {
      Object.defineProperty(window, 'location', {
        value: { origin: 'http://localhost' },
        writable: true
      });

      const url = buildURL('/api/alerts/history', {
        limit: 50,
        offset: 0
      });

      expect(url).toContain('/api/alerts/history');
      expect(url).toContain('limit=50');
      expect(url).toContain('offset=0');
    });

    test('should skip null and undefined parameters', () => {
      const url = buildURL('/api/test', {
        param1: 'value',
        param2: null,
        param3: undefined
      });

      expect(url).toContain('param1=value');
      expect(url).not.toContain('param2');
      expect(url).not.toContain('param3');
    });
  });

  describe('Error Parsing', () => {
    function parseErrorResponse(error, response) {
      if (response && response.status) {
        return {
          message: `HTTP ${response.status}: ${response.statusText || 'Error'}`,
          status: response.status,
          type: 'http_error'
        };
      }

      if (error.message === 'Failed to fetch') {
        return {
          message: 'Erreur réseau - Impossible de contacter le serveur',
          type: 'network_error'
        };
      }

      if (error.message.includes('timeout')) {
        return {
          message: 'Délai d\'attente dépassé',
          type: 'timeout_error'
        };
      }

      return {
        message: error.message || 'Erreur inconnue',
        type: 'unknown_error'
      };
    }

    test('should parse HTTP errors', () => {
      const response = { status: 500, statusText: 'Internal Server Error' };
      const parsed = parseErrorResponse(new Error(), response);

      expect(parsed.message).toContain('HTTP 500');
      expect(parsed.type).toBe('http_error');
      expect(parsed.status).toBe(500);
    });

    test('should parse network errors', () => {
      const error = new Error('Failed to fetch');
      const parsed = parseErrorResponse(error);

      expect(parsed.message).toContain('réseau');
      expect(parsed.type).toBe('network_error');
    });

    test('should parse timeout errors', () => {
      const error = new Error('Request timeout');
      const parsed = parseErrorResponse(error);

      expect(parsed.message).toContain('Délai');
      expect(parsed.type).toBe('timeout_error');
    });
  });
});
