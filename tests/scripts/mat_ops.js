// Test script: covers MatWrapper properties + cv.* bindings + createMat.
// Reads the input image, runs a small OpenCV pipeline, and outputs an
// encoded number proving the final dimensions/channels: W*10000+H*100+C.
const inputs = [{ name: "image", type: "image", required: true }];
const outputs = [{ name: "code", type: "float" }];
const params = [
    { name: "thresh", type: "int", default: 100 }
];

function execute(ctx) {
    const img = ctx.input("image");
    const t = ctx.param("thresh");

    // createMat is width-first: (W, H, C) -> cols=W, rows=H.
    const m = cv.createMat(10, 20, 3);
    if (m.width !== 10 || m.height !== 20 || m.channels !== 3) {
        throw new Error("createMat width-first mismatch");
    }

    const gray = cv.cvtColor(img, cv.COLOR_BGR2GRAY);
    const blurred = cv.blur(gray, 3, 3);
    const threshed = cv.threshold(blurred, t, 255, cv.THRESH_BINARY);

    const code = threshed.width * 10000 + threshed.height * 100 + threshed.channels;
    ctx.log(`mat_ops: in ${img.width}x${img.height}x${img.channels}, out code=${code}`);
    ctx.setOutput("code", code);
}