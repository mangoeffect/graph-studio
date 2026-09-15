// Test script: verifies JS exceptions propagate -> the task fails with a
// readable error message. Declares the image input (fed by the graph) but
// ignores it.
const inputs = [{ name: "image", type: "image", required: true }];
const outputs = [{ name: "value", type: "float" }];
const params = [];

function execute(ctx) {
    throw new Error("test error");
}