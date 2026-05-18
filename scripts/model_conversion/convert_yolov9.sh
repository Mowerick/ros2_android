#!/usr/bin/env bash
# Convert yolov9_s_pobed.pt (PyTorch) to NCNN format for Android ARM inference.
#
# Pipeline:
#   yolov9_s_pobed.pt
#     -> export.py --include torchscript --imgsz 352 640 --device cpu
#     -> pnnx inputshape=[1,3,352,640]f32
#     -> sed (fix Input layer shape annotation)
#     -> ncnnoptimize 65536 (FP16 storage)
#     -> yolov9_s_pobed.ncnn.{param,bin}

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: $(basename "$0") --yolov9-repo <path> [--model <path>] [--output-dir <path>]

  --yolov9-repo <path>   Path to cloned Vermin_Collector_ROS2_3D_Object_Detection repo.
                         export.py is read from <path>/object_detection/object_detection/export.py
  --model <path>         Path to yolov9_s_pobed.pt  (default: models/yolov9_s_pobed.pt)
  --output-dir <path>    Directory for output files  (default: output/)
  -h, --help             Show this help message
EOF
}

# Defaults
MODEL_PATH="$SCRIPT_DIR/models/yolov9_s_pobed.pt"
OUTPUT_DIR="$SCRIPT_DIR/output"
YOLOV9_REPO=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --yolov9-repo)
            YOLOV9_REPO="$2"; shift 2 ;;
        --model)
            MODEL_PATH="$2"; shift 2 ;;
        --output-dir)
            OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

# Validate required argument
if [[ -z "$YOLOV9_REPO" ]]; then
    echo "Error: --yolov9-repo is required." >&2
    usage
    exit 1
fi

EXPORT_PY="$YOLOV9_REPO/object_detection/object_detection/export.py"

# Check required tools
for tool in python3 pnnx ncnnoptimize; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Error: '$tool' not found on PATH. See README.md for installation instructions." >&2
        exit 1
    fi
done

# Validate inputs
if [[ ! -f "$MODEL_PATH" ]]; then
    echo "Error: Model file not found: $MODEL_PATH" >&2
    exit 1
fi

if [[ ! -f "$EXPORT_PY" ]]; then
    echo "Error: export.py not found at: $EXPORT_PY" >&2
    echo "  Make sure --yolov9-repo points to the Vermin_Collector_ROS2_3D_Object_Detection repo." >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Create temp working directory, cleaned up on exit
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "==> Working directory: $WORKDIR"
echo "==> Model: $MODEL_PATH"
echo "==> export.py: $EXPORT_PY"
echo "==> Output: $OUTPUT_DIR"
echo ""

cp "$MODEL_PATH" "$WORKDIR/weights.pt"

cd "$WORKDIR"

echo "==> Step 1/4: Exporting PyTorch model to TorchScript..."
python3 "$EXPORT_PY" --weights "$WORKDIR/weights.pt" --include torchscript --imgsz 352 640 --device cpu

if [[ ! -f weights.torchscript ]]; then
    echo "Error: weights.torchscript was not produced by export.py" >&2
    exit 1
fi

echo ""
echo "==> Step 2/4: Converting TorchScript to NCNN via PNNX..."
pnnx weights.torchscript "inputshape=[1,3,352,640]f32"

if [[ ! -f weights.ncnn.param ]]; then
    echo "Error: weights.ncnn.param was not produced by pnnx" >&2
    exit 1
fi

echo ""
echo "==> Step 3/4: Patching Input layer shape annotation..."
# The generated param omits the shape annotation on the Input layer; add it explicitly
# so the NCNN C++ inference code can read the correct H/W/C dimensions.
sed -i 's/^\(Input\s*in0\s*0 1 in0\)$/\1 0=640 1=352 2=3/' weights.ncnn.param

echo ""
echo "==> Step 4/4: Optimizing with ncnnoptimize (FP16 storage, flag 65536)..."
ncnnoptimize weights.ncnn.param weights.ncnn.bin weights.opt.ncnn.param weights.opt.ncnn.bin 65536

cp weights.opt.ncnn.param "$OUTPUT_DIR/yolov9_s_pobed.ncnn.param"
cp weights.opt.ncnn.bin   "$OUTPUT_DIR/yolov9_s_pobed.ncnn.bin"

echo ""
echo "Done. Output files:"
echo "  $OUTPUT_DIR/yolov9_s_pobed.ncnn.param"
echo "  $OUTPUT_DIR/yolov9_s_pobed.ncnn.bin"
