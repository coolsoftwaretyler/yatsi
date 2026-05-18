#include <emscripten/bind.h>
#include "yatsi.h"

using namespace emscripten;

// Wrapper: run typecheck and pack the result into an EvalResult
// so the web harness can use the same result-handling path.
static EvalResult typecheck_as_eval(const std::string& source) {
    TypeCheckResult tc = typecheck(source);
    std::string output;
    for (size_t i = 0; i < tc.warnings.size(); i++) {
        if (i > 0) output += "\n";
        output += tc.warnings[i];
    }
    return EvalResult{tc.success, "", "", 0, output};
}

// Wrapper: run dump_typed_ast and pack the result into an EvalResult.
static EvalResult dump_typed_ast_as_eval(const std::string& source) {
    std::string output = dump_typed_ast(source);
    return EvalResult{true, "", "", 0, output};
}

// Bind the evaluate method to JavaScript, allowing it to be called from JS and return an EvalResult object. The EvalResult struct is also bound so that its fields can be accessed from JavaScript.
EMSCRIPTEN_BINDINGS(yatsi) {
    value_object<EvalResult>("EvalResult")
        .field("success", &EvalResult::success)
        .field("error_type", &EvalResult::error_type)
        .field("error_message", &EvalResult::error_message)
        .field("phase", &EvalResult::phase)
        .field("console_output", &EvalResult::console_output);

    function("evaluate", &evaluate);
    function("typecheck_as_eval", &typecheck_as_eval);
    function("dump_typed_ast_as_eval", &dump_typed_ast_as_eval);
}
