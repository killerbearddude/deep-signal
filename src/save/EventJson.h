#pragma once

// Provides typed JSON serialization for persisted simulation events.
// SaveGameRepository stores event payloads as text, while this module owns the
// event-specific conversion rules and validation so JSON handling does not leak
// across the rest of the SQLite repository.

#include "sim/Events.h"

#include <string>
#include <string_view>

namespace deep::save {

// Returns the stable schema v1 event type name for a typed payload.
// The returned names are persisted in event_log.event_type and therefore must
// be migration-managed if renamed later.
[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload);

// Serializes one typed event payload to strict JSON object text for SQLite.
// Throws if the payload contains a non-finite numeric value that cannot be
// represented safely in save data.
[[nodiscard]] std::string eventPayloadToJson(const SimEventPayload& payload);

// Parses one event payload from schema v1 JSON text and validates that the
// required fields for eventType are present and have the expected types.
// Throws std::runtime_error on malformed or unknown payloads.
[[nodiscard]] SimEventPayload eventPayloadFromJson(std::string_view eventType, std::string_view json);

} // namespace deep::save
