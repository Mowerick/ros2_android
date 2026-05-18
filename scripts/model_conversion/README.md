# YOLOv9 / OSNet-AIN Model Conversion

Shell scripts to convert the PyTorch models used in the Android perception pipeline to NCNN format
for on-device CPU inference.

| Script | Input | Output |
|--------|-------|--------|
| `convert_yolov9.sh` | `yolov9_s_pobed.pt` (YOLOv9-S, CPB dataset) | `yolov9_s_pobed.ncnn.{param,bin}` |
| `convert_osnet.sh` | `osnet_ain_x1_0.pth` (OSNet-AIN x1.0, ImageNet) | `osnet_reid.ncnn.{param,bin}` |

The converted models are deployed to Android and loaded at runtime by the NCNN inference engine.

---

## Dependencies

### System tools

| Tool | Source | Notes |
|------|--------|-------|
| `ncnnoptimize` | [Tencent/ncnn](https://github.com/Tencent/ncnn) | Build with `-DNCNN_BUILD_TOOLS=ON` |
| `pnnx` | Installed via the pip wheel below | Provides the `pnnx` CLI binary |

Build ncnn with tools enabled:

```bash
cmake -DNCNN_BUILD_TOOLS=ON ..
make -j$(nproc)
sudo make install
```

### Python packages

Install with pip:

```bash
pip install torch torchvision          # CPU build is sufficient
pip install pnnx==20260112             # see note below
pip install onnx onnxscript
```

> [!NOTE]
> The `pnnx==20260112` wheel is a pre-built binary for **x86_64 Linux only**
> (`manylinux2014_x86_64`). It also installs the `pnnx` CLI.

**torchreid** is not on PyPI and must be installed from source (required for `convert_osnet.sh`):

```bash
git clone --branch v1.0.6 https://github.com/KaiyangZhou/deep-person-reid.git
cd deep-person-reid
pip install -r requirements.txt
pip install -e .
```

The following packages are pulled in as torchreid dependencies but may also be needed explicitly:

```
numpy  opencv-python  pillow  scipy  matplotlib  h5py
```

### For `convert_yolov9.sh` only

A local clone of the `Vermin_Collector_ROS2_3D_Object_Detection` repository is required.
The script reads `export.py` from `<repo>/object_detection/object_detection/export.py`.

---

## Usage

### Convert YOLOv9-S

```bash
bash convert_yolov9.sh \
  --yolov9-repo ~/uni_projects/ROS2/Vermin_Collector_ROS2_3D_Object_Detection \
  --model models/yolov9_s_pobed.pt \
  --output-dir output/
```

`--model` and `--output-dir` are optional and default to the values shown above.

### Convert OSNet-AIN

```bash
bash convert_osnet.sh \
  --model models/osnet_ain_x1_0.pth \
  --output-dir output/
```

Both `--model` and `--output-dir` are optional.

---

## Outputs

Both scripts write their output to `output/` (or the directory given with `--output-dir`):

```
output/
  yolov9_s_pobed.ncnn.param   # 69 KB - network topology
  yolov9_s_pobed.ncnn.bin     # 19 MB - weights (FP16)
  osnet_reid.ncnn.param       # 25 KB - network topology
  osnet_reid.ncnn.bin         #  4 MB - weights (FP16)
```

The FP16 optimization is applied by `ncnnoptimize` with flag `65536`.

---

## Conversion pipelines

**YOLOv9-S** (PNNX path - avoids Einsum operator incompatibility with direct ONNX export):

```
yolov9_s_pobed.pt
  -> export.py --include torchscript --imgsz 352 640 --device cpu
  -> weights.torchscript
  -> pnnx inputshape=[1,3,352,640]f32
  -> weights.ncnn.{param,bin}  (Input shape annotation patched via sed)
  -> ncnnoptimize 65536
  -> yolov9_s_pobed.ncnn.{param,bin}
```

**OSNet-AIN** (ONNX path - model is compatible with direct ONNX export):

```
osnet_ain_x1_0.pth
  -> torchreid.models.build_model + torch.onnx.export (opset 11, [1,3,256,128])
  -> osnet_reid.onnx  (validated: no Loop/If/Scan ops)
  -> pnnx inputshape=[1,3,256,128]f32
  -> ncnnoptimize 65536
  -> osnet_reid.ncnn.{param,bin}
```
