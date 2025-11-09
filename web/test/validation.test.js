/**
 * @file validation.test.js
 * @brief Unit tests for input validation and security
 */

import { describe, test, expect } from '@jest/globals';

describe('Input Validation and Security', () => {
  describe('IPv4 Address Validation', () => {
    function isValidIPv4(ip) {
      const ipv4Regex = /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
      return ipv4Regex.test(ip);
    }

    test('should accept valid IPv4 addresses', () => {
      expect(isValidIPv4('192.168.1.1')).toBe(true);
      expect(isValidIPv4('10.0.0.1')).toBe(true);
      expect(isValidIPv4('172.16.0.1')).toBe(true);
      expect(isValidIPv4('255.255.255.255')).toBe(true);
      expect(isValidIPv4('0.0.0.0')).toBe(true);
    });

    test('should reject invalid IPv4 addresses', () => {
      expect(isValidIPv4('256.1.1.1')).toBe(false);
      expect(isValidIPv4('1.1.1.256')).toBe(false);
      expect(isValidIPv4('1.1.1')).toBe(false);
      expect(isValidIPv4('1.1.1.1.1')).toBe(false);
      expect(isValidIPv4('abc.def.ghi.jkl')).toBe(false);
      expect(isValidIPv4('')).toBe(false);
    });
  });

  describe('Port Number Validation', () => {
    function isValidPort(port) {
      const portNum = parseInt(port, 10);
      return !isNaN(portNum) && portNum >= 1 && portNum <= 65535;
    }

    test('should accept valid port numbers', () => {
      expect(isValidPort(80)).toBe(true);
      expect(isValidPort(443)).toBe(true);
      expect(isValidPort(1883)).toBe(true);
      expect(isValidPort(8080)).toBe(true);
      expect(isValidPort(1)).toBe(true);
      expect(isValidPort(65535)).toBe(true);
    });

    test('should reject invalid port numbers', () => {
      expect(isValidPort(0)).toBe(false);
      expect(isValidPort(-1)).toBe(false);
      expect(isValidPort(65536)).toBe(false);
      expect(isValidPort('abc')).toBe(false);
      expect(isValidPort('')).toBe(false);
    });
  });

  describe('MQTT Topic Validation', () => {
    function isValidMQTTTopic(topic) {
      if (!topic || typeof topic !== 'string') return false;
      if (topic.length === 0 || topic.length > 65535) return false;
      if (topic.includes('\0')) return false;
      if (topic.includes('+') && !isValidWildcard(topic)) return false;
      if (topic.includes('#') && !isValidWildcard(topic)) return false;
      return true;
    }

    function isValidWildcard(topic) {
      // Simplified wildcard validation
      const parts = topic.split('/');
      for (let i = 0; i < parts.length; i++) {
        const part = parts[i];
        if (part === '+') continue;
        if (part === '#' && i === parts.length - 1) continue;
        if (part.includes('+') || part.includes('#')) return false;
      }
      return true;
    }

    test('should accept valid MQTT topics', () => {
      expect(isValidMQTTTopic('home/temperature')).toBe(true);
      expect(isValidMQTTTopic('sensors/battery/voltage')).toBe(true);
      expect(isValidMQTTTopic('tinybms/status')).toBe(true);
      expect(isValidMQTTTopic('a')).toBe(true);
    });

    test('should accept valid wildcards', () => {
      expect(isValidMQTTTopic('home/+/temperature')).toBe(true);
      expect(isValidMQTTTopic('sensors/#')).toBe(true);
      expect(isValidMQTTTopic('+/+/voltage')).toBe(true);
    });

    test('should reject invalid MQTT topics', () => {
      expect(isValidMQTTTopic('')).toBe(false);
      expect(isValidMQTTTopic(null)).toBe(false);
      expect(isValidMQTTTopic('topic\0null')).toBe(false);
      expect(isValidMQTTTopic('home/temp#erature')).toBe(false);
      expect(isValidMQTTTopic('sensors/#/data')).toBe(false);
    });
  });

  describe('Baudrate Validation', () => {
    function isValidBaudrate(baudrate) {
      const validRates = [
        9600, 19200, 38400, 57600, 115200,
        230400, 460800, 921600
      ];
      return validRates.includes(parseInt(baudrate, 10));
    }

    test('should accept valid baudrates', () => {
      expect(isValidBaudrate(9600)).toBe(true);
      expect(isValidBaudrate(115200)).toBe(true);
      expect(isValidBaudrate(921600)).toBe(true);
    });

    test('should reject invalid baudrates', () => {
      expect(isValidBaudrate(1200)).toBe(false);
      expect(isValidBaudrate(999999)).toBe(false);
      expect(isValidBaudrate('fast')).toBe(false);
    });
  });

  describe('GPIO Pin Validation', () => {
    function isValidGPIO(pin) {
      const pinNum = parseInt(pin, 10);
      if (isNaN(pinNum)) return false;

      // ESP32 valid GPIO pins (simplified - some restrictions apply)
      const validPins = [
        0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23,
        25, 26, 27, 32, 33, 34, 35, 36, 37, 38, 39
      ];

      return validPins.includes(pinNum);
    }

    test('should accept valid GPIO pins', () => {
      expect(isValidGPIO(2)).toBe(true);
      expect(isValidGPIO(4)).toBe(true);
      expect(isValidGPIO(32)).toBe(true);
    });

    test('should reject invalid GPIO pins', () => {
      expect(isValidGPIO(6)).toBe(false);  // Not available on ESP32
      expect(isValidGPIO(100)).toBe(false);
      expect(isValidGPIO(-1)).toBe(false);
      expect(isValidGPIO('A0')).toBe(false);
    });
  });

  describe('Path Traversal Prevention', () => {
    function isSafePath(path) {
      if (!path || typeof path !== 'string') return false;

      const dangerousPatterns = [
        '../', '..\\',
        '%2e%2e/', '%2E%2E/',
        '%2e%2e\\', '%2E%2E\\',
        '..%2f', '..%2F',
        '..%5c', '..%5C',
        '%252e',
        '..../',
        '\0'
      ];

      const lowerPath = path.toLowerCase();

      for (const pattern of dangerousPatterns) {
        if (lowerPath.includes(pattern.toLowerCase())) {
          return false;
        }
      }

      return true;
    }

    test('should accept safe paths', () => {
      expect(isSafePath('/index.html')).toBe(true);
      expect(isSafePath('/src/css/tabler.min.css')).toBe(true);
      expect(isSafePath('/api/status')).toBe(true);
    });

    test('should reject path traversal attempts', () => {
      expect(isSafePath('../etc/passwd')).toBe(false);
      expect(isSafePath('../../secrets')).toBe(false);
      expect(isSafePath('%2e%2e/config')).toBe(false);
      expect(isSafePath('..%2fpasswd')).toBe(false);
      expect(isSafePath('....//etc')).toBe(false);
    });

    test('should reject double-encoded attempts', () => {
      expect(isSafePath('%252e%252e/passwd')).toBe(false);
    });

    test('should reject null bytes', () => {
      expect(isSafePath('file\0.txt')).toBe(false);
    });
  });

  describe('XSS Prevention', () => {
    function escapeHtml(text) {
      if (!text) return '';
      const div = document.createElement('div');
      div.textContent = text;
      return div.innerHTML;
    }

    function stripScripts(html) {
      const div = document.createElement('div');
      div.innerHTML = html;

      const scripts = div.getElementsByTagName('script');
      while (scripts.length > 0) {
        scripts[0].parentNode.removeChild(scripts[0]);
      }

      return div.innerHTML;
    }

    test('escapeHtml should escape dangerous characters', () => {
      expect(escapeHtml('<script>alert("XSS")</script>'))
        .toBe('&lt;script&gt;alert("XSS")&lt;/script&gt;');

      expect(escapeHtml('<img src=x onerror=alert(1)>'))
        .toContain('&lt;img');

      expect(escapeHtml('<a href="javascript:alert(1)">'))
        .toContain('&lt;a href=');
    });

    test('escapeHtml should handle quotes', () => {
      const escaped = escapeHtml('"Hello" and \'World\'');
      expect(escaped).toBe('"Hello" and \'World\'');
    });

    test('stripScripts should remove script tags', () => {
      const dirty = '<div>Safe</div><script>alert("XSS")</script>';
      const clean = stripScripts(dirty);

      expect(clean).toContain('<div>Safe</div>');
      expect(clean).not.toContain('<script>');
    });

    test('should handle empty and null inputs', () => {
      expect(escapeHtml('')).toBe('');
      expect(escapeHtml(null)).toBe('');
      expect(escapeHtml(undefined)).toBe('');
    });
  });

  describe('Form Input Sanitization', () => {
    function sanitizeInput(input, maxLength = 255) {
      if (!input || typeof input !== 'string') return '';

      // Trim whitespace
      let sanitized = input.trim();

      // Limit length
      if (sanitized.length > maxLength) {
        sanitized = sanitized.substring(0, maxLength);
      }

      return sanitized;
    }

    test('should trim whitespace', () => {
      expect(sanitizeInput('  hello  ')).toBe('hello');
      expect(sanitizeInput('\ttest\n')).toBe('test');
    });

    test('should enforce length limits', () => {
      const longString = 'a'.repeat(1000);
      expect(sanitizeInput(longString, 100).length).toBe(100);
    });

    test('should handle null and undefined', () => {
      expect(sanitizeInput(null)).toBe('');
      expect(sanitizeInput(undefined)).toBe('');
      expect(sanitizeInput('')).toBe('');
    });
  });

  describe('JSON Parsing Safety', () => {
    function safeParse(jsonString, fallback = null) {
      try {
        return JSON.parse(jsonString);
      } catch (error) {
        console.error('JSON parse error:', error);
        return fallback;
      }
    }

    test('should parse valid JSON', () => {
      const result = safeParse('{"key": "value"}');
      expect(result).toEqual({ key: 'value' });
    });

    test('should handle invalid JSON gracefully', () => {
      expect(safeParse('invalid json')).toBe(null);
      expect(safeParse('{broken')).toBe(null);
      expect(safeParse('')).toBe(null);
    });

    test('should use provided fallback', () => {
      const fallback = { default: true };
      expect(safeParse('invalid', fallback)).toEqual(fallback);
    });
  });

  describe('Number Validation', () => {
    function isValidNumber(value, min = -Infinity, max = Infinity) {
      const num = parseFloat(value);
      return !isNaN(num) && num >= min && num <= max;
    }

    test('should validate numbers within range', () => {
      expect(isValidNumber(50, 0, 100)).toBe(true);
      expect(isValidNumber(0, 0, 100)).toBe(true);
      expect(isValidNumber(100, 0, 100)).toBe(true);
    });

    test('should reject numbers outside range', () => {
      expect(isValidNumber(-1, 0, 100)).toBe(false);
      expect(isValidNumber(101, 0, 100)).toBe(false);
    });

    test('should reject non-numeric values', () => {
      expect(isValidNumber('abc')).toBe(false);
      expect(isValidNumber('')).toBe(false);
      expect(isValidNumber(null)).toBe(false);
    });

    test('should accept string numbers', () => {
      expect(isValidNumber('42', 0, 100)).toBe(true);
      expect(isValidNumber('3.14', 0, 10)).toBe(true);
    });
  });

  describe('CAN ID Validation', () => {
    function isValidCANId(id, extended = false) {
      const numId = parseInt(id, 16);
      if (isNaN(numId)) return false;

      if (extended) {
        return numId >= 0 && numId <= 0x1FFFFFFF; // 29-bit
      } else {
        return numId >= 0 && numId <= 0x7FF; // 11-bit
      }
    }

    test('should validate standard CAN IDs (11-bit)', () => {
      expect(isValidCANId('0x100')).toBe(true);
      expect(isValidCANId('0x7FF')).toBe(true);
      expect(isValidCANId('0x000')).toBe(true);
    });

    test('should validate extended CAN IDs (29-bit)', () => {
      expect(isValidCANId('0x10000', true)).toBe(true);
      expect(isValidCANId('0x1FFFFFFF', true)).toBe(true);
    });

    test('should reject invalid CAN IDs', () => {
      expect(isValidCANId('0x800')).toBe(false);  // Too large for standard
      expect(isValidCANId('0x20000000', true)).toBe(false);  // Too large for extended
      expect(isValidCANId('invalid')).toBe(false);
    });
  });

  describe('WiFi SSID Validation', () => {
    function isValidSSID(ssid) {
      if (!ssid || typeof ssid !== 'string') return false;
      if (ssid.length === 0 || ssid.length > 32) return false;
      return true;
    }

    test('should accept valid SSIDs', () => {
      expect(isValidSSID('MyNetwork')).toBe(true);
      expect(isValidSSID('Guest-WiFi-2024')).toBe(true);
      expect(isValidSSID('a')).toBe(true);
      expect(isValidSSID('A'.repeat(32))).toBe(true);
    });

    test('should reject invalid SSIDs', () => {
      expect(isValidSSID('')).toBe(false);
      expect(isValidSSID('A'.repeat(33))).toBe(false);
      expect(isValidSSID(null)).toBe(false);
    });
  });
});
