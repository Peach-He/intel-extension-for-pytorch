Releases
=============

## 1.10.0

The Intel® Extension for PyTorch\* 1.10 is on top of PyTorch 1.10. In this release, we polished the front end APIs. The APIs are more simplible, stable and straightforward now. According to PyTorch community recommendation, we changed the underhood device from `XPU` to `CPU`. With this change, the model and tensor does not need to be converted to the extension device to get performance improvement. It simplifies the model changes.

Besides that, we continuously optimize the Transformer\* and CNN models by fusing more operators and applying NHWC. We measured the 1.10 performance on Torchvison and HugginFace. As expected, 1.10 can speed up the two model zones.

### Highlights

- Change the package name to `intel_extension_for_pytorch` while the original package name is `intel_pytorch_extension`. This change targets to avoid any potential legal issues.

<table align="center">
<tbody>
<tr>
<td>v1.9.0-cpu</td>
<td>v1.10.0-cpu</td>
</tr>
<tr>
<td>

```
import intel_extension_for_pytorch as ipex
```
</td>
<td>

```
import intel_extension_for_pytorch as ipex
```
</td>
</tr>
</tbody>
</table>

- The underhood device is changed from the extension-specific device(`XPU`) to the standard CPU device which aligns with PyTorch CPU device design regardless of the dispatch mechanism and operator register mechanism. The interface impactions are that the model does not need to be converted to the extension device explicitly.

<table align="center">
<tbody>
<tr>
<td>v1.9.0-cpu</td>
<td>v1.10.0-cpu</td>
</tr>
<tr>
<td>

```
import torch
import torchvision.models as models

# Import the extension
import intel_extension_for_pytorch as ipex

resnet18 = models.resnet18(pretrained = True)

# Explicitly convert the model to the extension device
resnet18_xpu = resnet18.to(ipex.DEVICE)
```
</td>
<td>

```
import torch
import torchvision.models as models

# Import the extension
import intel_extension_for_pytorch as ipex

resnet18 = models.resnet18(pretrained = True)
```
</td>
</tr>
</tbody>
</table>

- Compared to v1.9.0, v1.10.0 follows PyTorch AMP API(`torch.cpu.amp`) to support auto-mixed-precision. `torch.cpu.amp` provides convenience for auto data type conversion at runtime. Currently, `torch.cpu.amp` only supports `torch.bfloat16`. It is the default lower precision floating point data type when `torch.cpu.amp` is enabled. `torch.cpu.amp` primarily benefits on Intel CPU with BFloat16 instruction set support.

```
import torch
class SimpleNet(torch.nn.Module):
    def __init__(self):
        super(SimpleNet, self).__init__()
        self.conv = torch.nn.Conv2d(64, 128, (3, 3), stride=(2, 2), padding=(1, 1), bias=False)

    def forward(self, x):
        return self.conv(x)
```

<table align="center">
<tbody>
<tr>
<td>v1.9.0-cpu</td>
<td>v1.10.0-cpu</td>
</tr>
<tr>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

# Automatically mix precision
ipex.enable_auto_mixed_precision(mixed_dtype = torch.bfloat16)

model = SimpleNet().eval()
x = torch.rand(64, 64, 224, 224)
with torch.no_grad():
    model = torch.jit.trace(model, x)
    model = torch.jit.freeze(model)
    y = model(x)
```
</td>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

model = SimpleNet().eval()
x = torch.rand(64, 64, 224, 224)
with torch.cpu.amp.autocast(), torch.no_grad():
    model = torch.jit.trace(model, x)
    model = torch.jit.freeze(model)
    y = model(x)
```
</td>
</tr>
</tbody>
</table>

- The 1.10 release provides the INT8 calibration as an experimental feature while it only supports post-training static quantization now. Compared to 1.9.0, the fronted APIs for qutization is more straightforward and ease-of-use.

```
import torch
import torch.nn as nn
import intel_extension_for_pytorch as ipex

class MyModel(nn.Module):
    def __init__(self):
        super(MyModel, self).__init__()
        self.conv = nn.Conv2d(10, 10, 3)
        
    def forward(self, x):
        x = self.conv(x)
        return x

model = MyModel().eval()

# user dataset for calibration.
xx_c = [torch.randn(1, 10, 28, 28) for i in range(2))
# user dataset for validation.
xx_v = [torch.randn(1, 10, 28, 28) for i in range(20))
```
  - Clibration
<table align="center">
<tbody>
<tr>
<td>v1.9.0-cpu</td>
<td>v1.10.0-cpu</td>
</tr>
<tr>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

# Convert the model to the Extension device
model = Model().to(ipex.DEVICE)

# Create a configuration file to save quantization parameters.
conf = ipex.AmpConf(torch.int8)
with torch.no_grad():
    for x in xx_c:
        # Run the model under calibration mode to collect quantization parameters
        with ipex.AutoMixPrecision(conf, running_mode='calibration'):
            y = model(x.to(ipex.DEVICE))
# Save the configuration file
conf.save('configure.json')
```
</td>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

conf = ipex.quantization.QuantConf(qscheme=torch.per_tensor_affine)
with torch.no_grad():
    for x in xx_c:
        with ipex.quantization.calibrate(conf):
            y = model(x)

conf.save('configure.json')
```
</td>
</tr>
</tbody>
</table>

 - Inference
 <table align="center">
<tbody>
<tr>
<td>v1.9.0-cpu</td>
<td>v1.10.0-cpu</td>
</tr>
<tr>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

# Convert the model to the Extension device
model = Model().to(ipex.DEVICE)
conf = ipex.AmpConf(torch.int8, 'configure.json')
with torch.no_grad():
    for x in cali_dataset:
        with ipex.AutoMixPrecision(conf, running_mode='inference'):
            y = model(x.to(ipex.DEVICE))
```
</td>
<td>

```
# Import the extension
import intel_extension_for_pytorch as ipex

conf = ipex.quantization.QuantConf('configure.json')

with torch.no_grad():
    trace_model = ipex.quantization.convert(model, conf, example_input)
    for x in xx_v:
        y = trace_model(x)
```
</td>
</tr>
</tbody>
</table>


- This release introduces the `optimize` API at python front end to optimize the model and optimizer for training. The new API both supports FP32 and BF16, inference and training.

- Runtime Extension (Experimental) provides a runtime CPU pool API to bind threads to cores. It also features async tasks. Please **note**: Intel® Extension for PyTorch\* Runtime extension is still in the **POC** stage. The API is subject to change. More detailed descriptions are available in the extension documentation.

### Known Issues

- `omp_set_num_threads` function failed to change OpenMP threads number of oneDNN operators if it was set before.

  `omp_set_num_threads` function is provided in Intel® Extension for PyTorch\* to change the number of threads used with OpenMP. However, it failed to change the number of OpenMP threads if it was set before.

  pseudo-code:

  ```
  omp_set_num_threads(6)
  model_execution()
  omp_set_num_threads(4)
  same_model_execution_again()
  ```

  **Reason:** oneDNN primitive descriptor stores the omp number of threads. Current oneDNN integration caches the primitive descriptor in IPEX. So if we use runtime extension with oneDNN based pytorch/ipex operation, the runtime extension fails to change the used omp number of threads.

- Low performance with INT8 support for dynamic shapes

  The support for dynamic shapes in Intel® Extension for PyTorch\* INT8 integration is still working in progress. For the use cases where the input shapes are dynamic, for example, inputs of variable image sizes in an object detection task or of variable sequence lengths in NLP tasks, the Intel® Extension for PyTorch\* INT8 path may slow down the model inference. In this case, please utilize stock PyTorch INT8 functionality.

- Low throughput with DLRM FP32 Train

  A 'Sparse Add' [PR](https://github.com/pytorch/pytorch/pull/23057) is pending review. The issue will be fixed when the PR is merged.

### What's Changed
**Full Changelog**: https://github.com/intel/intel-extension-for-pytorch/compare/v1.9.0...v1.10.0+cpu-rc3

## 1.9.0

### What's New

* Rebased the Intel Extension for Pytorch from PyTorch-1.8.0 to the official PyTorch-1.9.0 release.
* Support binary installation.

  `python -m pip install torch_ipex==1.9.0 -f https://software.intel.com/ipex-whl-stable`
* Support the C++ library. The third party App can link the Intel-Extension-for-PyTorch C++ library to enable the particular optimizations.

## 1.8.0

### What's New

* Rebased the Intel Extension for Pytorch from Pytorch -1.7.0 to the official Pytorch-1.8.0 release. The new XPU device type has been added into Pytorch-1.8.0(49786), don’t need to patch PyTorch to enable Intel Extension for Pytorch anymore
* Upgraded the oneDNN from v1.5-rc to v1.8.1
* Updated the README file to add the sections to introduce supported customized operators, supported fusion patterns, tutorials and joint blogs with stakeholders

## 1.2.0

### What's New

* We rebased the Intel Extension for pytorch from Pytorch -1.5rc3 to the official Pytorch-1.7.0 release. It will have performance improvement with the new Pytorch-1.7 support.
* Device name was changed from DPCPP to XPU.

  We changed the device name from DPCPP to XPU to align with the future Intel GPU product for heterogeneous computation.
* Enabled the launcher for end users.
* We enabled the launch script which helps users launch the program for training and inference, then automatically setup the strategy for multi-thread, multi-instance, and memory allocator. Please refer to the launch script comments for more details.

### Performance Improvement

* This upgrade provides better INT8 optimization with refined auto mixed-precision API.
* More operators are optimized for the int8 inference and bfp16 training of some key workloads, like MaskRCNN, SSD-ResNet34, DLRM, RNNT.

### Others

* Bug fixes
  * This upgrade fixes the issue that saving the model trained by Intel extension for PyTorch caused errors.
  * This upgrade fixes the issue that Intel extension for PyTorch was slower than pytorch proper for Tacotron2.
* New custom operators

  This upgrade adds several custom operators: ROIAlign, RNN, FrozenBatchNorm, nms.
* Optimized operators/fusion

  This upgrade optimizes several operators: tanh, log_softmax, upsample, embeddingbad and enables int8 linear fusion.
* Performance

  The release has daily automated testing for the supported models: ResNet50, ResNext101, Huggingface Bert, DLRM, Resnext3d, MaskRNN, SSD-ResNet34. With the extension imported, it can bring up to 2x INT8 over FP32 inference performance improvements on the 3rd Gen Intel Xeon scalable processors (formerly codename Cooper Lake).

### Known issues

* Multi-node training still encounter hang issues after several iterations. The fix will be included in the next official release.

## 1.1.0

### What's New

* Added optimization for training with FP32 data type & BF16 data type. All the optimized FP32/BF16 backward operators include:
  * Conv2d
  * Relu
  * Gelu
  * Linear
  * Pooling
  * BatchNorm
  * LayerNorm
  * Cat
  * Softmax
  * Sigmoid
  * Split
  * Embedding_bag
  * Interaction
  * MLP
* More fusion patterns are supported and validated in the release, see table:

  |Fusion Patterns|Release|
  |--|--|
  |Conv + Sum|v1.0|
  |Conv + BN|v1.0|
  |Conv + Relu|v1.0|
  |Linear + Relu|v1.0|
  |Conv + Eltwise|v1.1|
  |Linear + Gelu|v1.1|

* Add docker support
* [Alpha] Multi-node training with oneCCL support.
* [Alpha] INT8 inference optimization.

### Performance

* The release has daily automated testing for the supported models: ResNet50, ResNext101, [Huggingface Bert](https://github.com/huggingface/transformers), [DLRM](https://github.com/intel/optimized-models/tree/master/pytorch/dlrm), [Resnext3d](https://github.com/XiaobingSuper/Resnext3d-for-video-classification), [Transformer](https://github.com/pytorch/fairseq/blob/master/fairseq/models/transformer.py). With the extension imported, it can bring up to 1.2x~1.7x BF16 over FP32 training performance improvements on the 3rd Gen Intel Xeon scalable processors (formerly codename Cooper Lake).

### Known issue

* Some workloads may crash after several iterations on the extension with [jemalloc](https://github.com/jemalloc/jemalloc) enabled.

## 1.0.2

* Rebase torch CCL patch to PyTorch 1.5.0-rc3

## 1.0.1-Alpha

* Static link oneDNN library
* Check AVX512 build option
* Fix the issue that cannot normally invoke `enable_auto_optimization`

## 1.0.0-Alpha

### What's New

* Auto Operator Optimization

  Intel Extension for PyTorch will automatically optimize the operators of PyTorch when importing its python package. It will significantly improve the computation performance if the input tensor and the model is converted to the extension device.

* Auto Mixed Precision
  Currently, the extension has supported bfloat16. It streamlines the work to enable a bfloat16 model. The feature is controlled by `enable_auto_mix_precision`. If you enable it, the extension will run the operator with bfloat16 automatically to accelerate the operator computation.

### Performance Result

We collected the performance data of some models on the Intel Cooper Lake platform with 1 socket and 28 cores. Intel Cooper Lake introduced AVX512 BF16 instructions which could improve the bfloat16 computation significantly. The detail is as follows (The data is the speedup ratio and the baseline is upstream PyTorch).

||Imperative - Operator Injection|Imperative - Mixed Precision|JIT- Operator Injection|JIT - Mixed Precision|
|:--:|:--:|:--:|:--:|:--:|
|RN50|2.68|5.01|5.14|9.66|
|ResNet3D|3.00|4.67|5.19|8.39|
|BERT-LARGE|0.99|1.40|N/A|N/A|

We also measured the performance of ResNeXt101, Transformer-FB, DLRM, and YOLOv3 with the extension. We observed that the performance could be significantly improved by the extension as expected.

### Known issue

* [#10](https://github.com/intel/intel-extension-for-pytorch/issues/10) All data types have not been registered for DPCPP
* [#37](https://github.com/intel/intel-extension-for-pytorch/issues/37) MaxPool can't get nan result when input's value is nan

### NOTE

The extension supported PyTorch v1.5.0-rc3. Support for other PyTorch versions is working in progress.
