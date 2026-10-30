#include "app/ui/expression.h"

#include <cstring>

namespace ui {

namespace {
const char* const kExpressionNames[kExpressionCount] = {"normal", "happy", "surprised",
                                                         "sleepy", "angry", "sad"};
}  // namespace

const char* expressionName(Expression e) { return kExpressionNames[size_t(e) % kExpressionCount]; }

int findExpression(const char* name) {
  for (size_t i = 0; i < kExpressionCount; i++) {
    if (strcmp(kExpressionNames[i], name) == 0) return int(i);
  }
  return -1;
}

}  // namespace ui
