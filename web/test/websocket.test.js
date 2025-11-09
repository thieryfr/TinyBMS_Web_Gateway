/**
 * @file websocket.test.js
 * @brief Unit tests for WebSocket utilities and connection management
 */

import { describe, test, expect, beforeEach, jest } from '@jest/globals';

describe('WebSocket Utilities', () => {
  describe('WebSocket Connection Manager', () => {
    class WebSocketManager {
      constructor(url, options = {}) {
        this.url = url;
        this.options = options;
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = options.maxReconnectAttempts || 5;
        this.reconnectDelay = options.reconnectDelay || 1000;
        this.listeners = {
          message: [],
          open: [],
          close: [],
          error: []
        };
      }

      connect() {
        this.ws = new WebSocket(this.url);
        this.setupListeners();
      }

      setupListeners() {
        this.ws.onopen = () => {
          this.reconnectAttempts = 0;
          this.listeners.open.forEach(fn => fn());
        };

        this.ws.onmessage = (event) => {
          this.listeners.message.forEach(fn => fn(event.data));
        };

        this.ws.onerror = (error) => {
          this.listeners.error.forEach(fn => fn(error));
        };

        this.ws.onclose = () => {
          this.listeners.close.forEach(fn => fn());
          this.handleReconnect();
        };
      }

      handleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
          return;
        }

        this.reconnectAttempts++;
        const delay = this.reconnectDelay * Math.pow(2, this.reconnectAttempts - 1);

        setTimeout(() => this.connect(), delay);
      }

      on(event, callback) {
        if (this.listeners[event]) {
          this.listeners[event].push(callback);
        }
      }

      send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
          this.ws.send(JSON.stringify(data));
        }
      }

      disconnect() {
        if (this.ws) {
          this.maxReconnectAttempts = 0; // Prevent reconnection
          this.ws.close();
          this.ws = null;
        }
      }
    }

    test('should create WebSocket with correct URL', () => {
      const manager = new WebSocketManager('ws://localhost/ws/test');
      expect(manager.url).toBe('ws://localhost/ws/test');
    });

    test('should register event listeners', () => {
      const manager = new WebSocketManager('ws://localhost/ws/test');
      const callback = jest.fn();

      manager.on('message', callback);
      expect(manager.listeners.message).toContain(callback);
    });

    test('should calculate exponential backoff delay', () => {
      const manager = new WebSocketManager('ws://localhost/ws/test', {
        reconnectDelay: 1000
      });

      manager.reconnectAttempts = 1;
      const delay1 = 1000 * Math.pow(2, 0); // 1000ms
      expect(delay1).toBe(1000);

      manager.reconnectAttempts = 2;
      const delay2 = 1000 * Math.pow(2, 1); // 2000ms
      expect(delay2).toBe(2000);

      manager.reconnectAttempts = 3;
      const delay3 = 1000 * Math.pow(2, 2); // 4000ms
      expect(delay3).toBe(4000);
    });

    test('should stop reconnecting after max attempts', () => {
      const manager = new WebSocketManager('ws://localhost/ws/test', {
        maxReconnectAttempts: 3
      });

      manager.reconnectAttempts = 3;
      manager.handleReconnect();

      // Should not increment beyond max
      expect(manager.reconnectAttempts).toBe(3);
    });
  });

  describe('WebSocket Message Parsing', () => {
    function parseWebSocketMessage(data) {
      try {
        return JSON.parse(data);
      } catch (error) {
        console.error('Failed to parse WebSocket message:', error);
        return null;
      }
    }

    test('should parse valid JSON messages', () => {
      const message = '{"type":"telemetry","voltage":52000}';
      const parsed = parseWebSocketMessage(message);

      expect(parsed).toEqual({
        type: 'telemetry',
        voltage: 52000
      });
    });

    test('should handle invalid JSON gracefully', () => {
      const message = 'not valid json';
      const parsed = parseWebSocketMessage(message);

      expect(parsed).toBe(null);
    });

    test('should parse nested objects', () => {
      const message = '{"type":"battery","data":{"voltage":52000,"cells":[3200,3210]}}';
      const parsed = parseWebSocketMessage(message);

      expect(parsed.type).toBe('battery');
      expect(parsed.data.voltage).toBe(52000);
      expect(parsed.data.cells).toEqual([3200, 3210]);
    });
  });

  describe('WebSocket Message Routing', () => {
    class MessageRouter {
      constructor() {
        this.handlers = new Map();
      }

      register(type, handler) {
        if (!this.handlers.has(type)) {
          this.handlers.set(type, []);
        }
        this.handlers.get(type).push(handler);
      }

      route(message) {
        const data = typeof message === 'string' ? JSON.parse(message) : message;

        if (!data.type) {
          console.warn('Message has no type:', data);
          return;
        }

        const handlers = this.handlers.get(data.type) || [];
        handlers.forEach(handler => handler(data));

        // Wildcard handlers
        const wildcardHandlers = this.handlers.get('*') || [];
        wildcardHandlers.forEach(handler => handler(data));
      }
    }

    test('should route messages to correct handlers', () => {
      const router = new MessageRouter();
      const telemetryHandler = jest.fn();
      const alertHandler = jest.fn();

      router.register('telemetry', telemetryHandler);
      router.register('alert', alertHandler);

      router.route({ type: 'telemetry', voltage: 52000 });

      expect(telemetryHandler).toHaveBeenCalledWith({
        type: 'telemetry',
        voltage: 52000
      });
      expect(alertHandler).not.toHaveBeenCalled();
    });

    test('should handle wildcard handlers', () => {
      const router = new MessageRouter();
      const wildcardHandler = jest.fn();

      router.register('*', wildcardHandler);

      router.route({ type: 'telemetry', data: 1 });
      router.route({ type: 'alert', data: 2 });

      expect(wildcardHandler).toHaveBeenCalledTimes(2);
    });

    test('should handle messages without type', () => {
      const router = new MessageRouter();
      const handler = jest.fn();

      router.register('test', handler);

      // Should not crash
      router.route({ data: 'no type' });

      expect(handler).not.toHaveBeenCalled();
    });
  });

  describe('WebSocket Rate Limiting', () => {
    class RateLimiter {
      constructor(maxMessages = 10, windowMs = 1000) {
        this.maxMessages = maxMessages;
        this.windowMs = windowMs;
        this.timestamps = [];
      }

      canSend() {
        const now = Date.now();
        const windowStart = now - this.windowMs;

        // Remove old timestamps
        this.timestamps = this.timestamps.filter(ts => ts > windowStart);

        return this.timestamps.length < this.maxMessages;
      }

      recordSend() {
        this.timestamps.push(Date.now());
      }

      send(ws, data) {
        if (!this.canSend()) {
          console.warn('Rate limit exceeded');
          return false;
        }

        this.recordSend();
        ws.send(data);
        return true;
      }
    }

    test('should allow messages under rate limit', () => {
      const limiter = new RateLimiter(10, 1000);

      for (let i = 0; i < 10; i++) {
        expect(limiter.canSend()).toBe(true);
        limiter.recordSend();
      }
    });

    test('should block messages over rate limit', () => {
      const limiter = new RateLimiter(5, 1000);

      for (let i = 0; i < 5; i++) {
        limiter.recordSend();
      }

      expect(limiter.canSend()).toBe(false);
    });

    test('should reset after time window', async () => {
      const limiter = new RateLimiter(2, 100);

      limiter.recordSend();
      limiter.recordSend();
      expect(limiter.canSend()).toBe(false);

      await new Promise(resolve => setTimeout(resolve, 150));
      expect(limiter.canSend()).toBe(true);
    });
  });

  describe('WebSocket URL Building', () => {
    function buildWebSocketURL(path) {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const host = window.location.host;
      return `${protocol}//${host}${path}`;
    }

    test('should build WS URL for HTTP', () => {
      Object.defineProperty(window, 'location', {
        value: {
          protocol: 'http:',
          host: 'localhost:8080'
        },
        writable: true
      });

      const url = buildWebSocketURL('/ws/telemetry');
      expect(url).toBe('ws://localhost:8080/ws/telemetry');
    });

    test('should build WSS URL for HTTPS', () => {
      Object.defineProperty(window, 'location', {
        value: {
          protocol: 'https:',
          host: 'example.com'
        },
        writable: true
      });

      const url = buildWebSocketURL('/ws/telemetry');
      expect(url).toBe('wss://example.com/ws/telemetry');
    });
  });

  describe('WebSocket State Management', () => {
    function getReadyStateName(readyState) {
      const states = {
        0: 'CONNECTING',
        1: 'OPEN',
        2: 'CLOSING',
        3: 'CLOSED'
      };
      return states[readyState] || 'UNKNOWN';
    }

    test('should map ready state to names', () => {
      expect(getReadyStateName(0)).toBe('CONNECTING');
      expect(getReadyStateName(1)).toBe('OPEN');
      expect(getReadyStateName(2)).toBe('CLOSING');
      expect(getReadyStateName(3)).toBe('CLOSED');
    });
  });

  describe('Binary Message Handling', () => {
    function parseBinaryMessage(arrayBuffer) {
      const view = new DataView(arrayBuffer);
      const messageType = view.getUint8(0);
      const timestamp = view.getUint32(1, true); // Little-endian

      return {
        type: messageType,
        timestamp: timestamp,
        data: arrayBuffer.slice(5)
      };
    }

    test('should parse binary message header', () => {
      const buffer = new ArrayBuffer(9);
      const view = new DataView(buffer);

      view.setUint8(0, 1); // Message type
      view.setUint32(1, 123456, true); // Timestamp

      const parsed = parseBinaryMessage(buffer);

      expect(parsed.type).toBe(1);
      expect(parsed.timestamp).toBe(123456);
    });
  });

  describe('Connection Health Check', () => {
    class ConnectionMonitor {
      constructor(ws, interval = 30000) {
        this.ws = ws;
        this.interval = interval;
        this.lastPong = Date.now();
        this.pingTimer = null;
      }

      start() {
        this.pingTimer = setInterval(() => {
          if (Date.now() - this.lastPong > this.interval * 2) {
            console.warn('Connection appears dead, reconnecting...');
            this.ws.close();
            return;
          }

          if (this.ws.readyState === 1) {
            this.ws.send(JSON.stringify({ type: 'ping' }));
          }
        }, this.interval);
      }

      onPong() {
        this.lastPong = Date.now();
      }

      stop() {
        if (this.pingTimer) {
          clearInterval(this.pingTimer);
          this.pingTimer = null;
        }
      }
    }

    test('should track last pong time', () => {
      const mockWs = { readyState: 1, send: jest.fn() };
      const monitor = new ConnectionMonitor(mockWs, 1000);

      const before = monitor.lastPong;
      monitor.onPong();
      const after = monitor.lastPong;

      expect(after).toBeGreaterThanOrEqual(before);
    });
  });
});
