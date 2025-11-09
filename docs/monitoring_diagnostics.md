# Monitoring Diagnostics Event

The monitoring module periodically publishes an operational health snapshot on the
application event bus using the `APP_EVENT_ID_MONITORING_DIAGNOSTICS` event ID.
The payload is a UTF-8 JSON document providing counters for mutex timeouts,
JSON generation latency and queue pressure for event bus subscribers.

## JSON schema

```json
{
  "type": "monitoring_diagnostics",
  "timestamp_ms": 1700000000000,
  "mutex_timeouts": 0,
  "queue_saturation": {
    "publish_failures": 0,
    "last_failure_ms": 0,
    "dropped_events_total": 0,
    "consumer_count": 5
  },
  "snapshot_latency": {
    "avg_us": 1200,
    "max_us": 3200,
    "samples": 64
  }
}
```

* `type` – constant discriminator used by front-ends to recognise the
  diagnostics message.
* `timestamp_ms` – monotonic milliseconds (from `esp_timer_get_time`) captured
  when the snapshot was produced.
* `mutex_timeouts` – cumulative number of mutex acquisition timeouts observed
  by the monitoring module since boot. The counter is incremented whenever the
  monitoring mutex cannot be acquired within 100 ms.
* `queue_saturation.publish_failures` – number of times the monitoring module
  could not publish to the event bus because a subscriber queue was full.
* `queue_saturation.last_failure_ms` – timestamp (milliseconds) of the most
  recent publish failure, or `0` if no failure was observed.
* `queue_saturation.dropped_events_total` – aggregate count of dropped events
  reported by the event bus for all subscribers at the time the diagnostics
  message was generated.
* `queue_saturation.consumer_count` – number of subscribers enumerated when
  computing the dropped event total.
* `snapshot_latency.avg_us` – arithmetic mean (microseconds) of the telemetry
  snapshot JSON build time.
* `snapshot_latency.max_us` – maximum JSON build latency recorded so far
  (microseconds).
* `snapshot_latency.samples` – number of latency samples included in the
  average and maximum calculations.

The monitoring module emits the diagnostics payload at startup and then every
five seconds. The payload is also routed to WebSocket clients subscribed to the
web server event feed for visibility in operational dashboards.
