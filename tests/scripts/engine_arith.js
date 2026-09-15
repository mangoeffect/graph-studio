// Test script: covers the JS engine + TG.Math + ES2020 arrow functions.
// No image input; reads params and outputs a computed float.
const inputs = [];
const outputs = [{ name: "value", type: "float" }];
const params = [
    { name: "a", type: "float", default: 10 },
    { name: "b", type: "float", default: 30 }
];

function execute(ctx) {
    const a = ctx.param("a");
    const b = ctx.param("b");

    const clamped = TG.Math.clamp(15, 0, 10);   // 10
    const lerped = TG.Math.lerp(0, 100, 0.5);   // 50
    const mul = (x, y) => x * y;                // ES2020 arrow function
    const prod = mul(a, b);                     // a*b

    const total = clamped + lerped + prod;      // 10 + 50 + a*b
    ctx.log(`engine_arith: clamped=${clamped} lerped=${lerped} prod=${prod}`);
    ctx.setOutput("value", total);
}