/**
 * @file jest.config.js
 * @brief Jest configuration for ES6 modules testing
 */

export default {
  // Use jsdom environment for browser APIs
  testEnvironment: 'jsdom',

  // Enable ES6 modules support
  transform: {},

  // Map .js extensions for ES6 modules
  moduleNameMapper: {
    '^(\\.{1,2}/.*)\\.js$': '$1'
  },

  // Test file patterns
  testMatch: [
    '**/test/**/*.test.js',
    '**/__tests__/**/*.js',
    '**/?(*.)+(spec|test).js'
  ],

  // Coverage configuration
  collectCoverageFrom: [
    'src/js/**/*.js',
    '!src/js/lib/**',           // Exclude external libraries
    '!src/js/tabler.min.js',    // Exclude minified files
    '!**/node_modules/**',
    '!**/test/**'
  ],

  // Coverage thresholds
  coverageThreshold: {
    global: {
      branches: 70,
      functions: 70,
      lines: 70,
      statements: 70
    }
  },

  // Coverage reporters
  coverageReporters: [
    'text',
    'text-summary',
    'html',
    'lcov'
  ],

  // Setup files
  setupFilesAfterEnv: ['<rootDir>/test/setup.js'],

  // Verbose output
  verbose: true,

  // Clear mocks between tests
  clearMocks: true,

  // Restore mocks between tests
  restoreMocks: true,

  // Maximum number of workers
  maxWorkers: '50%',

  // Test timeout
  testTimeout: 10000,

  // Globals
  globals: {
    'window': {},
    'document': {},
    'navigator': {},
    'localStorage': {},
    'sessionStorage': {}
  }
};
