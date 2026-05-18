#!/usr/bin/env bash
# Convert osnet_ain_x1_0.pth (PyTorch) to NCNN format for Android ARM inference.
#
# Pipeline:
#   osnet_ain_x1_0.pth
#     -> torchreid model load + torch.onnx.export (opset 18, shape [1,3,256,128])
#     -> onnx validation (no Loop/If/Scan ops)
#     -> pnnx inputshape=[1,3,256,128]f32
#     -> ncnnoptimize 65536 (FP16 storage)
#     -> osnet_reid.ncnn.{param,bin}

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--model <path>] [--output-dir <path>]

  --model <path>       Path to osnet_ain_x1_0.pth  (default: models/osnet_ain_x1_0.pth)
  --output-dir <path>  Directory for output files   (default: output/)
  -h, --help           Show this help message
EOF
}

# Defaults
MODEL_PATH="$SCRIPT_DIR/models/osnet_ain_x1_0.pth"
OUTPUT_DIR="$SCRIPT_DIR/output"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
    --model)
        MODEL_PATH="$2"
        shift 2
        ;;
    --output-dir)
        OUTPUT_DIR="$2"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown argument: $1" >&2
        usage
        exit 1
        ;;
    esac
done

# Check required tools
for tool in python3 pnnx ncnnoptimize; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Error: '$tool' not found on PATH. See README.md for installation instructions." >&2
        exit 1
    fi
done

# Check required Python packages
for pkg in torchreid onnx; do
    if ! python3 -c "import $pkg" &>/dev/null; then
        echo "Error: Python package '$pkg' not available. See README.md for installation instructions." >&2
        exit 1
    fi
done

# Validate input
if [[ ! -f "$MODEL_PATH" ]]; then
    echo "Error: Model file not found: $MODEL_PATH" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Create temp working directory, cleaned up on exit
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "==> Working directory: $WORKDIR"
echo "==> Model: $MODEL_PATH"
echo "==> Output: $OUTPUT_DIR"
echo ""

cp "$MODEL_PATH" "$WORKDIR/weights.pth"

# Write the export script into the working directory.
# This is the exact logic from yolov9/flake.nix (osnet-reid buildPhase).
cat >"$WORKDIR/export.py" <<'EOF'
import torch
import torchreid
import onnx
import sys

model = torchreid.models.build_model(name='osnet_ain_x1_0', num_classes=1000, pretrained=False, loss='softmax')
checkpoint = torch.load('weights.pth', map_location='cpu')

if isinstance(checkpoint, dict):
    state_dict = checkpoint.get('state_dict', checkpoint.get('model', checkpoint))
else:
    state_dict = checkpoint

model.load_state_dict(state_dict, strict=False)
model.eval()

torch.manual_seed(0)
torch.onnx.export(
    model,
    torch.randn(1, 3, 256, 128),
    "osnet_reid.onnx",
    export_params=True,
    opset_version=18,
    do_constant_folding=True,
    input_names=['images:0'],
    output_names=['features:0']
)

onnx_model = onnx.load("osnet_reid.onnx")
onnx.checker.check_model(onnx_model)

ops = [node.op_type for node in onnx_model.graph.node]
if set(ops) & {'Loop', 'If', 'Scan'}:
    sys.exit(1)
EOF

cd "$WORKDIR"

echo "==> Step 1/3: Exporting OSNet-AIN to ONNX via torchreid..."
PYTHONHASHSEED=0 python3 export.py

if [[ ! -f osnet_reid.onnx ]]; then
    echo "Error: osnet_reid.onnx was not produced by export.py" >&2
    exit 1
fi

echo ""
echo "==> Step 2/3: Converting ONNX to NCNN via PNNX..."
pnnx osnet_reid.onnx inputshape=[1,3,256,128]f32

if [[ ! -f osnet_reid.ncnn.param ]]; then
    echo "Error: osnet_reid.ncnn.param was not produced by pnnx" >&2
    exit 1
fi

echo ""
echo "==> Step 3/3: Optimizing with ncnnoptimize (FP16 storage, flag 65536)..."
ncnnoptimize osnet_reid.ncnn.param osnet_reid.ncnn.bin osnet_reid_opt.ncnn.param osnet_reid_opt.ncnn.bin 65536

cp osnet_reid_opt.ncnn.param "$OUTPUT_DIR/osnet_ain_x1_0.ncnn.param"
cp osnet_reid_opt.ncnn.bin "$OUTPUT_DIR/osnet_ain_x1_0.ncnn.bin"

echo ""
echo "Done. Output files:"
echo "  $OUTPUT_DIR/osnet_ain_x1_0.ncnn.param"
echo "  $OUTPUT_DIR/osnet_ain_x1_0.ncnn.bin"
