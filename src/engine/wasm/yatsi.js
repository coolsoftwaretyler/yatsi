// Placeholder — replaced by Emscripten build output from build-engine.sh
// This stub allows the project to build/typecheck without having built the WASM engine.
export default function createYatsi() {
  return Promise.resolve({
    evaluate(source) {
      return {
        success: false,
        error_type: "YatsiNotBuiltError",
        error_message: "WASM engine not built. Run ./build-engine.sh first.",
        phase: 2,
        console_output: "",
      };
    },
  });
}
