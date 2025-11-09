function getRuntime() {
  const now = Date.now();
  return {
    timestamp_ms: now,
    uptime_s: 86_450,
    boot_count: 4,
    cycle_count: 128,
    reset_reason: 'ESP_RST_POWERON',
    firmware: 'v1.4.0-test',
    last_boot: new Date(now - 86_450 * 1000).toISOString(),
    total_heap_bytes: 320000,
    free_heap_bytes: 180500,
    min_free_heap_bytes: 128000,
    cpu_load: { core0: 41.3, core1: 47.9 },
    event_loop: { avg_latency_ms: 3.4, max_latency_ms: 8.9 },
  };
}

function getEventBus() {
  return {
    dropped: {
      total: 21,
      by_consumer: [
        { name: 'mqtt_gateway', dropped: 12 },
        { name: 'web_server', dropped: 5 },
        { name: 'logger', dropped: 4 },
      ],
    },
    blocking: {
      total: 7,
      by_consumer: [
        { name: 'mqtt_gateway', blocking: 4 },
        { name: 'logger', blocking: 2 },
        { name: 'web_server', blocking: 1 },
      ],
    },
    queues: [
      { name: 'telemetry', used: 8, capacity: 20 },
      { name: 'events', used: 14, capacity: 32 },
      { name: 'can_frames', used: 5, capacity: 16 },
    ],
  };
}

function getTasks() {
  return [
    {
      name: 'main',
      state: 'running',
      cpu_percent: 22.1,
      stack_high_water_mark: 2100,
      core: 0,
      runtime_ticks: 1284398,
    },
    {
      name: 'mqtt',
      state: 'ready',
      cpu_percent: 14.4,
      stack_high_water_mark: 1580,
      core: 1,
      runtime_ticks: 932144,
    },
    {
      name: 'event_bus',
      state: 'blocked',
      cpu_percent: 9.1,
      stack_high_water_mark: 1310,
      core: 0,
      runtime_ticks: 601445,
    },
  ];
}

function getModules() {
  return [
    {
      name: 'event_bus',
      status: 'warning',
      detail: '7 blocages recensés',
      last_event: new Date(Date.now() - 2 * 60 * 1000).toISOString(),
    },
    {
      name: 'mqtt_gateway',
      status: 'warning',
      detail: '12 drops depuis le boot',
      last_event: new Date(Date.now() - 5 * 60 * 1000).toISOString(),
    },
    {
      name: 'logger',
      status: 'ok',
      detail: 'Aucun drop récent',
      last_event: new Date(Date.now() - 12 * 60 * 1000).toISOString(),
    },
  ];
}

module.exports = {
  getRuntime,
  getEventBus,
  getTasks,
  getModules,
};
