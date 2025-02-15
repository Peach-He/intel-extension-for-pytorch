Installation Guide
==================

## System Requirements

|Category|Content|
|--|--|
|Compiler|GCC version greater than 7\*|
|Operating System|CentOS 7, RHEL 8, Ubuntu newer than 18.04|
|Python|3.6, 3.7, 3.8, 3.9|

\* Verified with GCC 7, 8 and 9.

## Install PyTorch

You need to make sure PyTorch is installed in order to get the extension working properly. For each PyTorch release, we have a corresponding release of the extension. Here is the PyTorch versions that we support and the mapping relationship:

|PyTorch Version|Extension Version|
|--|--|
|[v1.10.0](https://github.com/pytorch/pytorch/tree/v1.10.0 "v1.10.0")|[v1.10.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.10.0)|
|[v1.9.0](https://github.com/pytorch/pytorch/tree/v1.9.0 "v1.9.0")|[v1.9.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.9.0)|
|[v1.8.0](https://github.com/pytorch/pytorch/tree/v1.8.0 "v1.8.0")|[v1.8.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.8.0)|
|[v1.7.0](https://github.com/pytorch/pytorch/tree/v1.7.0 "v1.7.0")|[v1.2.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.2.0)|
|[v1.5.0-rc3](https://github.com/pytorch/pytorch/tree/v1.5.0-rc3 "v1.5.0-rc3")|[v1.1.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.1.0)|
|[v1.5.0-rc3](https://github.com/pytorch/pytorch/tree/v1.5.0-rc3 "v1.5.0-rc3")|[v1.0.2](https://github.com/intel/intel-extension-for-pytorch/tree/v1.0.2)|
|[v1.5.0-rc3](https://github.com/pytorch/pytorch/tree/v1.5.0-rc3 "v1.5.0-rc3")|[v1.0.1](https://github.com/intel/intel-extension-for-pytorch/tree/v1.0.1)|
|[v1.5.0-rc3](https://github.com/pytorch/pytorch/tree/v1.5.0-rc3 "v1.5.0-rc3")|[v1.0.0](https://github.com/intel/intel-extension-for-pytorch/tree/v1.0.0)|

Here is an example showing how to install PyTorch (1.10.0). For more details, please refer to [pytorch.org](https://pytorch.org/)

```
python -m pip install torch==1.10.0+cpu -f https://download.pytorch.org/whl/cpu/torch_stable.html
```

---

**Note:**

For the extension version earlier than 1.8.0, a patch has to be manually applied to PyTorch source code. Please check previous installation guide.

From 1.8.0, compiling PyTorch from source is not required. If you still want to compile PyTorch, please follow instructions [here](https://github.com/pytorch/pytorch#installation). Please make sure to checkout the correct PyTorch version according to the table above.

---

## Install via wheel file

Prebuilt wheel files are available starting from 1.8.0 release. We recommend you to install the latest version (1.10.0) with the following commands:

```
python -m pip install intel_extension_for_pytorch==1.10.0 -f https://software.intel.com/ipex-whl-stable
python -m pip install psutil
```

**Note:** Wheel files availability for Python versions

| Extension Version | Python 3.6 | Python 3.7 | Python 3.8 | Python 3.9 |
| :--: | :--: | :--: | :--: | :--: |
| 1.10.0 | ✔️ | ✔️ | ✔️ | ✔️ |
| 1.9.0 | ✔️ | ✔️ | ✔️ | ✔️ |
| 1.8.0 |  | ✔️ |  |  |

**Note:** The wheel files released are compiled with AVX-512 instruction set support only. They cannot be running on hardware platforms that don't support AVX-512 instruction set. Please compile from source with AVX2 support in this case.

**Note:** For version prior to 1.10.0, please use package name `torch_ipex`, rather than `intel_extension_for_pytorch`.

## Install via source compilation

```bash
git clone --recursive https://github.com/intel/intel-extension-for-pytorch
cd intel-extension-for-pytorch
git checkout release/1.10

# if you are updating an existing checkout
git submodule sync
git submodule update --init --recursive

# run setup.py to compile and install the binaries
# if you need to compile from source with AVX2 support, please uncomment the following line.
# export AVX2=1
python setup.py install
```

## Install C++ SDK

|Version|Pre-cxx11 ABI|cxx11 ABI|
|--|--|--|
| 1.10.0 | [intel-ext-pt-cpu-libtorch-shared-with-deps-1.10.0+cpu.zip](https://intel-optimized-pytorch.s3.cn-north-1.amazonaws.com.cn/wheels/v1.10/intel-ext-pt-cpu-libtorch-shared-with-deps-1.10.0%2Bcpu.zip) | [intel-ext-pt-cpu-libtorch-cxx11-abi-shared-with-deps-1.10.0+cpu.zip](https://intel-optimized-pytorch.s3.cn-north-1.amazonaws.com.cn/wheels/v1.10/intel-ext-pt-cpu-libtorch-cxx11-abi-shared-with-deps-1.10.0%2Bcpu.zip) |

**Usage:** Donwload one zip file above according to your scenario, unzip it and follow the [C++ example](./examples.html#c).
