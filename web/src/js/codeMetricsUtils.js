export function getNestedValue(source, path) {
    if (!source || !path) return undefined;
    const segments = Array.isArray(path) ? path : String(path).split('.');
    let current = source;
    for (const segment of segments) {
        if (current == null) {
            return undefined;
        }
        current = current[segment];
    }
    return current;
}

export function coerceNumber(value) {
    const number = typeof value === 'string' ? Number.parseFloat(value) : value;
    return Number.isFinite(number) ? number : null;
}

function toConsumerEntries(value, metricKey) {
    if (!value) return [];

    if (Array.isArray(value)) {
        return value
            .map((item) => ({
                name: item?.name ?? item?.id ?? item?.consumer ?? '--',
                value: coerceNumber(item?.[metricKey] ?? item?.value ?? item?.count ?? item?.total ?? item),
            }))
            .filter((entry) => entry.name && entry.value != null);
    }

    if (typeof value === 'object') {
        const arrayLike = value.by_consumer || value.consumers || value.entries;
        if (Array.isArray(arrayLike)) {
            return toConsumerEntries(arrayLike, metricKey);
        }

        return Object.entries(value)
            .map(([name, data]) => ({
                name,
                value: coerceNumber(
                    data && typeof data === 'object'
                        ? data[metricKey] ?? data.value ?? data.count ?? data.total
                        : data,
                ),
            }))
            .filter((entry) => entry.name && entry.value != null);
    }

    return [];
}

function mergeConsumers(dropEntries, blockingEntries) {
    const map = new Map();

    dropEntries.forEach((entry) => {
        const existing = map.get(entry.name) ?? { name: entry.name, dropped: 0, blocking: 0 };
        existing.dropped = entry.value;
        map.set(entry.name, existing);
    });

    blockingEntries.forEach((entry) => {
        const existing = map.get(entry.name) ?? { name: entry.name, dropped: 0, blocking: 0 };
        existing.blocking = entry.value;
        map.set(entry.name, existing);
    });

    return Array.from(map.values()).sort((a, b) => {
        const totalA = (a.dropped ?? 0) + (a.blocking ?? 0);
        const totalB = (b.dropped ?? 0) + (b.blocking ?? 0);
        return totalB - totalA;
    });
}

function toQueueEntries(value) {
    if (!value) return [];

    if (Array.isArray(value)) {
        return value
            .map((entry) => ({
                name: entry?.name ?? entry?.id ?? entry?.queue ?? '--',
                used: coerceNumber(entry?.used ?? entry?.messages_waiting ?? entry?.pending),
                capacity: coerceNumber(entry?.capacity ?? entry?.queue_capacity ?? entry?.size),
            }))
            .filter((entry) => entry.name);
    }

    if (typeof value === 'object') {
        const arrayLike = value.by_consumer || value.consumers || value.entries;
        if (Array.isArray(arrayLike)) {
            return toQueueEntries(arrayLike);
        }

        return Object.entries(value).map(([name, data]) => ({
            name,
            used: coerceNumber(
                data && typeof data === 'object'
                    ? data.used ?? data.messages_waiting ?? data.pending
                    : data,
            ),
            capacity: coerceNumber(data?.capacity ?? data?.queue_capacity ?? data?.size),
        }));
    }

    return [];
}

function sumValues(entries, fallback = null) {
    if (!entries.length) return fallback;
    const total = entries.reduce((acc, entry) => acc + (entry.value ?? 0), 0);
    return Number.isFinite(total) ? total : fallback;
}

export function normalizeEventBusMetrics(eventBus) {
    const droppedEntries = toConsumerEntries(
        eventBus?.dropped_by_consumer ??
            eventBus?.drops ??
            eventBus?.dropped?.by_consumer ??
            eventBus?.drops?.by_consumer ??
            eventBus?.dropped?.consumers ??
            eventBus?.metrics?.dropped,
        'dropped',
    );

    const blockingEntries = toConsumerEntries(
        eventBus?.blocking_by_consumer ??
            eventBus?.blocked_by_consumer ??
            eventBus?.blocking?.by_consumer ??
            eventBus?.blocked?.by_consumer ??
            eventBus?.blocking?.consumers ??
            eventBus?.metrics?.blocking,
        'blocking',
    );

    const consumers = mergeConsumers(droppedEntries, blockingEntries);

    const droppedTotal =
        coerceNumber(
            eventBus?.dropped_total ??
                eventBus?.drops_total ??
                eventBus?.dropped?.total ??
                eventBus?.drops?.total ??
                eventBus?.metrics?.dropped_total,
        ) ?? sumValues(droppedEntries, 0);

    const blockingTotal =
        coerceNumber(
            eventBus?.blocking_total ??
                eventBus?.blocked_total ??
                eventBus?.blocking?.total ??
                eventBus?.blocked?.total ??
                eventBus?.metrics?.blocking_total,
        ) ?? sumValues(blockingEntries, 0);

    const queueDepth = toQueueEntries(
        eventBus?.queue_depth ?? eventBus?.queues ?? eventBus?.queue_metrics ?? eventBus?.metrics?.queues,
    );

    return {
        droppedTotal: droppedTotal ?? null,
        blockingTotal: blockingTotal ?? null,
        consumers,
        queueDepth,
    };
}
