#include <emscripten/bind.h>
#include "yatsi.h"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(yatsi) {
    value_object<EvalResult>("EvalResult")
        .field("success", &EvalResult::success)
        .field("error_type", &EvalResult::error_type)
        .field("error_message", &EvalResult::error_message)
        .field("phase", &EvalResult::phase)
        .field("console_output", &EvalResult::console_output);

    function("evaluate", &evaluate);
}
