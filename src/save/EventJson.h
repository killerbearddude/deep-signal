#pragma once

// Responsibility: convert typed event payloads to/from the JSON text stored by
// schema v10. Stable event names and fields are a persistence contract separate
// from display wording. This module owns payload conversion, not event ordering,
// reference integrity, or database transactions; those are checked elsewhere.

#include "sim/Events.h"

#include <string>
#include <string_view>

namespace deep::save {

// Returns the stable persisted event type name for a typed payload.
// The returned names are persisted in event_log.event_type and therefore must
// be migration-managed if renamed later.
[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload);

// Returns owned object text for one borrowed payload; rejects non-finite numeric
// fields. A system nlohmann header selects the full JSON implementation, otherwise
// a local serializer handles the limited strings emitted by current commands.
// The fallback does not escape every JSON control character; callers must not
// assume arbitrary string content is portable between the two implementations.
[[nodiscard]] std::string eventPayloadToJson(const SimEventPayload& payload);

// Returns an owned typed payload, borrowing both views only during the call.
// Unknown event types and detected parse/range failures throw; nlohmann failures
// may use its own exception types. The local fallback expects compact flat
// objects and is not a complete JSON syntax/type validator. GameState validation
// must still check domain values and references after decoding.
[[nodiscard]] SimEventPayload eventPayloadFromJson(std::string_view eventType, std::string_view json);

} // namespace deep::save
