#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

// Facial expressions every character understands. Each character decides how
// to show them; previews, the settings page and serial commands use the names.
enum class Expression : uint8_t { Normal, Happy, Surprised, Sleepy, Angry, Sad, Count };

constexpr size_t kExpressionCount = size_t(Expression::Count);

const char* expressionName(Expression e);
int findExpression(const char* name);  // -1 if unknown

}  // namespace ui
