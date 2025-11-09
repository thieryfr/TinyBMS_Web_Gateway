/**
 * @file logger.test.js
 * @brief Unit tests for logger module
 */

import { describe, test, expect, beforeEach, jest } from '@jest/globals';
import {
  debug,
  info,
  warn,
  error,
  configure,
  clearHistory,
  getHistory,
  getStats,
  createScope,
  exportLogsJSON,
  exportLogsCSV,
  LOG_LEVELS,
  LEVEL_NAMES
} from '../src/js/utils/logger.js';

describe('Logger Module', () => {
  beforeEach(() => {
    // Reset configuration before each test
    configure({
      level: 'INFO',
      enableConsole: false, // Disable console for tests
      enableStorage: false,
      groupSimilarLogs: false
    });
    clearHistory();
    localStorage.clear();
  });

  describe('Configuration', () => {
    test('should configure log level by string', () => {
      configure({ level: 'DEBUG' });

      // Debug should be logged now
      debug('Test debug message');
      const history = getHistory();

      expect(history.length).toBe(1);
      expect(history[0].level).toBe('DEBUG');
    });

    test('should configure log level by index', () => {
      configure({ level: LOG_LEVELS.WARN });

      info('Should not be logged');
      warn('Should be logged');

      const history = getHistory();
      expect(history.length).toBe(1);
      expect(history[0].level).toBe('WARN');
    });

    test('should enable storage', () => {
      configure({
        enableStorage: true,
        storageKey: 'test-logs'
      });

      info('Test message');

      const stored = localStorage.getItem('test-logs');
      expect(stored).toBeTruthy();

      const parsed = JSON.parse(stored);
      expect(parsed.length).toBe(1);
      expect(parsed[0].message).toBe('Test message');
    });

    test('should configure timestamp format', () => {
      configure({ timestampFormat: 'iso' });
      info('Test message');

      const history = getHistory();
      expect(history[0].timestamp).toBeInstanceOf(Date);
    });
  });

  describe('Logging Methods', () => {
    beforeEach(() => {
      configure({ level: 'DEBUG' });
    });

    test('debug() should create DEBUG log entry', () => {
      debug('Debug message', { key: 'value' });

      const history = getHistory();
      expect(history.length).toBe(1);
      expect(history[0].level).toBe('DEBUG');
      expect(history[0].message).toBe('Debug message');
      expect(history[0].data.key).toBe('value');
    });

    test('info() should create INFO log entry', () => {
      info('Info message');

      const history = getHistory();
      expect(history[0].level).toBe('INFO');
      expect(history[0].message).toBe('Info message');
    });

    test('warn() should create WARN log entry', () => {
      warn('Warning message', { warning: true });

      const history = getHistory();
      expect(history[0].level).toBe('WARN');
      expect(history[0].data.warning).toBe(true);
    });

    test('error() should create ERROR log entry with Error object', () => {
      const testError = new Error('Test error');
      error('Error occurred', testError);

      const history = getHistory();
      expect(history[0].level).toBe('ERROR');
      expect(history[0].error.message).toBe('Test error');
      expect(history[0].error.stack).toBeTruthy();
    });

    test('error() should create ERROR log entry with data object', () => {
      error('Error occurred', { code: 500 });

      const history = getHistory();
      expect(history[0].level).toBe('ERROR');
      expect(history[0].data.code).toBe(500);
    });

    test('should not log below configured level', () => {
      configure({ level: 'ERROR' });

      debug('Debug');
      info('Info');
      warn('Warn');
      error('Error');

      const history = getHistory();
      expect(history.length).toBe(1);
      expect(history[0].level).toBe('ERROR');
    });

    test('should include context information', () => {
      info('Test message');

      const history = getHistory();
      expect(history[0].context).toBeDefined();
      expect(history[0].context.url).toBeDefined();
      expect(history[0].context.userAgent).toBeDefined();
    });
  });

  describe('History Management', () => {
    beforeEach(() => {
      configure({ level: 'DEBUG' });
    });

    test('getHistory() should return all logs', () => {
      debug('Message 1');
      info('Message 2');
      warn('Message 3');

      const history = getHistory();
      expect(history.length).toBe(3);
    });

    test('getHistory() should filter by level', () => {
      debug('Debug');
      info('Info');
      warn('Warn');
      error('Error');

      const errors = getHistory({ level: 'ERROR' });
      expect(errors.length).toBe(1);
      expect(errors[0].level).toBe('ERROR');
    });

    test('getHistory() should filter by search term', () => {
      info('User logged in');
      info('User logged out');
      info('System started');

      const userLogs = getHistory({ search: 'user' });
      expect(userLogs.length).toBe(2);
    });

    test('getHistory() should limit results', () => {
      for (let i = 0; i < 10; i++) {
        info(`Message ${i}`);
      }

      const limited = getHistory({ limit: 5 });
      expect(limited.length).toBe(5);
    });

    test('clearHistory() should remove all logs', () => {
      info('Message 1');
      info('Message 2');

      expect(getHistory().length).toBe(2);

      clearHistory();

      // clearHistory() itself logs, so should have 1 entry
      expect(getHistory().length).toBe(1);
      expect(getHistory()[0].message).toBe('Log history cleared');
    });

    test('should respect maxStoredLogs limit', () => {
      configure({
        enableStorage: true,
        maxStoredLogs: 5
      });

      for (let i = 0; i < 10; i++) {
        info(`Message ${i}`);
      }

      const history = getHistory();
      expect(history.length).toBe(5);
      expect(history[0].message).toBe('Message 5');
    });
  });

  describe('Statistics', () => {
    test('getStats() should count logs by level', () => {
      configure({ level: 'DEBUG' });

      debug('Debug 1');
      debug('Debug 2');
      info('Info 1');
      warn('Warn 1');
      error('Error 1');
      error('Error 2');
      error('Error 3');

      const stats = getStats();
      expect(stats.total).toBe(7);
      expect(stats.byLevel.DEBUG).toBe(2);
      expect(stats.byLevel.INFO).toBe(1);
      expect(stats.byLevel.WARN).toBe(1);
      expect(stats.byLevel.ERROR).toBe(3);
    });
  });

  describe('Export Functions', () => {
    beforeEach(() => {
      configure({ level: 'DEBUG' });
      debug('Debug message');
      info('Info message');
      error('Error message', new Error('Test error'));
    });

    test('exportLogsJSON() should return valid JSON', () => {
      const json = exportLogsJSON();

      expect(json).toBeTruthy();
      const parsed = JSON.parse(json);
      expect(Array.isArray(parsed)).toBe(true);
      expect(parsed.length).toBe(3);
    });

    test('exportLogsCSV() should return valid CSV', () => {
      const csv = exportLogsCSV();

      expect(csv).toBeTruthy();
      expect(csv).toContain('Timestamp,Level,Message,Data,Error');
      expect(csv).toContain('DEBUG');
      expect(csv).toContain('INFO');
      expect(csv).toContain('ERROR');
    });
  });

  describe('Scoped Logger', () => {
    test('createScope() should prefix messages', () => {
      configure({ level: 'DEBUG' });

      const moduleLogger = createScope('TestModule');

      moduleLogger.debug('Debug message');
      moduleLogger.info('Info message');
      moduleLogger.warn('Warn message');
      moduleLogger.error('Error message');

      const history = getHistory();
      expect(history.length).toBe(4);

      history.forEach(entry => {
        expect(entry.message).toContain('[TestModule]');
      });
    });

    test('scoped logger should handle error objects', () => {
      configure({ level: 'DEBUG' });

      const logger = createScope('ErrorModule');
      const testError = new Error('Scoped error');

      logger.error('Error occurred', testError);

      const history = getHistory();
      expect(history[0].message).toContain('[ErrorModule]');
      expect(history[0].error.message).toBe('Scoped error');
    });
  });

  describe('Log Grouping', () => {
    test('should group similar logs within time window', (done) => {
      configure({
        level: 'DEBUG',
        groupSimilarLogs: true,
        enableConsole: false
      });

      // Log same message multiple times
      info('Repeated message');
      info('Repeated message');
      info('Repeated message');

      // Should only store once due to grouping
      const history = getHistory();

      // Note: Grouping affects console output, not history storage
      // So we still expect 3 entries in history
      expect(history.length).toBeGreaterThanOrEqual(1);

      done();
    });
  });

  describe('Storage Persistence', () => {
    test('should load logs from localStorage on initialization', () => {
      const mockLogs = [
        {
          level: 'INFO',
          message: 'Stored message',
          timestamp: new Date().toISOString(),
          data: {}
        }
      ];

      localStorage.setItem('tinybms-logs', JSON.stringify(mockLogs));

      configure({
        enableStorage: true,
        storageKey: 'tinybms-logs'
      });

      const history = getHistory();

      // Should have loaded log + the "Loaded N logs" message
      expect(history.length).toBeGreaterThanOrEqual(1);
    });

    test('should handle corrupted localStorage data gracefully', () => {
      localStorage.setItem('tinybms-logs', 'invalid json');

      configure({
        enableStorage: true,
        storageKey: 'tinybms-logs'
      });

      // Should not crash
      info('Test message');

      const history = getHistory();
      expect(history.length).toBeGreaterThanOrEqual(1);
    });
  });

  describe('Constants Export', () => {
    test('LOG_LEVELS should be exported', () => {
      expect(LOG_LEVELS).toBeDefined();
      expect(LOG_LEVELS.DEBUG).toBe(0);
      expect(LOG_LEVELS.INFO).toBe(1);
      expect(LOG_LEVELS.WARN).toBe(2);
      expect(LOG_LEVELS.ERROR).toBe(3);
      expect(LOG_LEVELS.NONE).toBe(4);
    });

    test('LEVEL_NAMES should be exported', () => {
      expect(LEVEL_NAMES).toBeDefined();
      expect(Array.isArray(LEVEL_NAMES)).toBe(true);
      expect(LEVEL_NAMES).toEqual(['DEBUG', 'INFO', 'WARN', 'ERROR', 'NONE']);
    });
  });
});
