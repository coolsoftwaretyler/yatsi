#include <emscripten/bind.h>
#include "yatsi.h"

using namespace emscripten;

// Bind the evaluate method to JavaScript, allowing it to be called from JS and return an EvalResult object. The EvalResult struct is also bound so that its fields can be accessed from JavaScript.
EMSCRIPTEN_BINDINGS(yatsi) {
    value_object<EvalResult>("EvalResult")
        .field("success", &EvalResult::success)
        .field("error_type", &EvalResult::error_type)
        .field("error_message", &EvalResult::error_message)
        .field("phase", &EvalResult::phase)
        .field("console_output", &EvalResult::console_output);

    function("evaluate", &evaluate);
}
