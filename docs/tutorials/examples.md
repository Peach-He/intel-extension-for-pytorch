Examples
========

## Training

### Single-instance Training

#### Code Changes Highlight

```
...
import torch
import intel_extension_for_pytorch as ipex
...
model = Model()
criterion = ...
optimizer = ...
model.train()
# For Float32
model, optimizer = ipex.optimize(model, optimizer=optimizer)
# For BFloat16
model, optimizer = ipex.optimize(model, optimizer=optimizer, dtype=torch.bfloat16)
...
# Setting memory_format to torch.channels_last could improve performance with 4D input data. This is optional.
data = data.to(memory_format=torch.channels_last)
optimizer.zero_grad()
output = model(data)
...
```

#### Complete - Float32

```
import torch
import torchvision
import intel_extension_for_pytorch as ipex

LR = 0.001
DOWNLOAD = True
DATA = 'datasets/cifar10/'

transform = torchvision.transforms.Compose([
    torchvision.transforms.Resize((224, 224)),
    torchvision.transforms.ToTensor(),
    torchvision.transforms.Normalize((0.5, 0.5, 0.5), (0.5, 0.5, 0.5))
])
train_dataset = torchvision.datasets.CIFAR10(
        root=DATA,
        train=True,
        transform=transform,
        download=DOWNLOAD,
)
train_loader = torch.utils.data.DataLoader(
        dataset=train_dataset,
        batch_size=128
)

model = torchvision.models.resnet50()
criterion = torch.nn.CrossEntropyLoss()
optimizer = torch.optim.SGD(model.parameters(), lr = LR, momentum=0.9)
model.train()
model, optimizer = ipex.optimize(model, optimizer=optimizer)

for batch_idx, (data, target) in enumerate(train_loader):
    # Setting memory_format to torch.channels_last could improve performance with 4D input data. This is optional.
    data = data.to(memory_format=torch.channels_last)
    optimizer.zero_grad()
    output = model(data)
    loss = criterion(output, target)
    loss.backward()
    optimizer.step()
    print(batch_idx)
torch.save({
     'model_state_dict': model.state_dict(),
     'optimizer_state_dict': optimizer.state_dict(),
     }, 'checkpoint.pth')
```

#### Complete - BFloat16

```
import torch
import torchvision
import intel_extension_for_pytorch as ipex

LR = 0.001
DOWNLOAD = True
DATA = 'datasets/cifar10/'

transform = torchvision.transforms.Compose([
    torchvision.transforms.Resize((224, 224)),
    torchvision.transforms.ToTensor(),
    torchvision.transforms.Normalize((0.5, 0.5, 0.5), (0.5, 0.5, 0.5))
])
train_dataset = torchvision.datasets.CIFAR10(
        root=DATA,
        train=True,
        transform=transform,
        download=DOWNLOAD,
)
train_loader = torch.utils.data.DataLoader(
        dataset=train_dataset,
        batch_size=128
)

model = torchvision.models.resnet50()
criterion = torch.nn.CrossEntropyLoss()
optimizer = torch.optim.SGD(model.parameters(), lr = LR, momentum=0.9)
model.train()
model, optimizer = ipex.optimize(model, optimizer=optimizer, dtype=torch.bfloat16)

for batch_idx, (data, target) in enumerate(train_loader):
    optimizer.zero_grad()
    with torch.cpu.amp.autocast():
        # Setting memory_format to torch.channels_last could improve performance with 4D input data. This is optional.
        data = data.to(memory_format=torch.channels_last)
        output = model(data)
        loss = criterion(output, target)
    loss.backward()
    optimizer.step()
    print(batch_idx)
torch.save({
     'model_state_dict': model.state_dict(),
     'optimizer_state_dict': optimizer.state_dict(),
     }, 'checkpoint.pth')
```

### Distributed Training

Distributed training with PyTorch DDP is accelerated by oneAPI Collective Communications Library Bindings for Pytorch\* (oneCCL Bindings for Pytorch\*). The extension supports FP32 and BF16 data types. More detailed information and examples are available at its [Github repo](https://github.com/intel/torch-ccl).

**Note:** When performing distributed training with BF16 data type, please use oneCCL Bindings for Pytorch\*. Due to a PyTorch limitation, distributed training with BF16 data type with Intel® Extension for PyTorch\* is not supported.

```
import os
import torch
import torch.distributed as dist
import torchvision
import torch_ccl
import intel_extension_for_pytorch as ipex

LR = 0.001
DOWNLOAD = True
DATA = 'datasets/cifar10/'

transform = torchvision.transforms.Compose([
    torchvision.transforms.Resize((224, 224)),
    torchvision.transforms.ToTensor(),
    torchvision.transforms.Normalize((0.5, 0.5, 0.5), (0.5, 0.5, 0.5))
])
train_dataset = torchvision.datasets.CIFAR10(
        root=DATA,
        train=True,
        transform=transform,
        download=DOWNLOAD,
)
train_loader = torch.utils.data.DataLoader(
        dataset=train_dataset,
        batch_size=128
)

os.environ['MASTER_ADDR'] = '127.0.0.1'
os.environ['MASTER_PORT'] = '29500'
os.environ['RANK'] = os.environ.get('PMI_RANK', 0)
os.environ['WORLD_SIZE'] = os.environ.get('PMI_SIZE', 1)
dist.init_process_group(
backend='ccl',
init_method='env://'
)

model = torchvision.models.resnet50()
criterion = torch.nn.CrossEntropyLoss()
optimizer = torch.optim.SGD(model.parameters(), lr = LR, momentum=0.9)
model.train()
model, optimizer = ipex.optimize(model, optimizer=optimizer)

model = torch.nn.parallel.DistributedDataParallel(model)

for batch_idx, (data, target) in enumerate(train_loader):
    optimizer.zero_grad()
    # Setting memory_format to torch.channels_last could improve performance with 4D input data. This is optional.
    data = data.to(memory_format=torch.channels_last)
    output = model(data)
    loss = criterion(output, target)
    loss.backward()
    optimizer.step()
    print('batch_id: {}'.format(batch_idx))
torch.save({
     'model_state_dict': model.state_dict(),
     'optimizer_state_dict': optimizer.state_dict(),
     }, 'checkpoint.pth')
```

## Inference

### Float32

#### Imperative Mode

##### Resnet50

```
import torch
import torchvision.models as models

model = models.resnet50(pretrained=True)
model.eval()
data = torch.rand(1, 3, 224, 224)

import intel_extension_for_pytorch as ipex
model = model.to(memory_format=torch.channels_last)
model = ipex.optimize(model)
data = data.to(memory_format=torch.channels_last)

with torch.no_grad():
  model(data)
```

##### BERT

```
import torch
from transformers import BertModel

model = BertModel.from_pretrained(args.model_name)
model.eval()

vocab_size = model.config.vocab_size
batch_size = 1
seq_length = 512
data = torch.randint(vocab_size, size=[batch_size, seq_length])

import intel_extension_for_pytorch as ipex
model = ipex.optimize(model)

with torch.no_grad():
  model(data)
```

#### TorchScript Mode

##### Resnet50

```
import torch
import torchvision.models as models

model = models.resnet50(pretrained=True)
model.eval()
data = torch.rand(1, 3, 224, 224)

import intel_extension_for_pytorch as ipex
model = model.to(memory_format=torch.channels_last)
model = ipex.optimize(model)
data = data.to(memory_format=torch.channels_last)

with torch.no_grad():
  d = torch.rand(1, 3, 224, 224)
  model = torch.jit.trace(model, d)
  model = torch.jit.freeze(model)

  model(data)
```

##### BERT

```
import torch
from transformers import BertModel

model = BertModel.from_pretrained(args.model_name)
model.eval()

vocab_size = model.config.vocab_size
batch_size = 1
seq_length = 512
data = torch.randint(vocab_size, size=[batch_size, seq_length])

import intel_extension_for_pytorch as ipex
model = ipex.optimize(model)

with torch.no_grad():
  d = torch.randint(vocab_size, size=[batch_size, seq_length])
  model = torch.jit.trace(model, (d,), check_trace=False, strict=False)
  model = torch.jit.freeze(model)

  model(data)
```

### BFloat16

#### Imperative Mode

##### Resnet50

```
import torch
import torchvision.models as models

model = models.resnet50(pretrained=True)
model.eval()
data = torch.rand(1, 3, 224, 224)

import intel_extension_for_pytorch as ipex
model = model.to(memory_format=torch.channels_last)
model = ipex.optimize(model, dtype=torch.bfloat16)
data = data.to(memory_format=torch.channels_last)

with torch.no_grad():
  with torch.cpu.amp.autocast():
    model(data)
```

##### BERT

```
import torch
from transformers import BertModel

model = BertModel.from_pretrained(args.model_name)
model.eval()

vocab_size = model.config.vocab_size
batch_size = 1
seq_length = 512
data = torch.randint(vocab_size, size=[batch_size, seq_length])

import intel_extension_for_pytorch as ipex
model = ipex.optimize(model, dtype=torch.bfloat16)

with torch.no_grad():
  with torch.cpu.amp.autocast():
    model(data)
```

#### TorchScript Mode

##### Resnet50

```
import torch
import torchvision.models as models

model = models.resnet50(pretrained=True)
model.eval()
data = torch.rand(1, 3, 224, 224)

import intel_extension_for_pytorch as ipex
model = model.to(memory_format=torch.channels_last)
model = ipex.optimize(model, dtype=torch.bfloat16)
data = data.to(memory_format=torch.channels_last)

with torch.no_grad():
  with torch.cpu.amp.autocast():
    model = torch.jit.trace(model, torch.rand(1, 3, 224, 224))
    model = torch.jit.freeze(model)

    model(data)
```

##### BERT

```
import torch
from transformers import BertModel

model = BertModel.from_pretrained(args.model_name)
model.eval()

vocab_size = model.config.vocab_size
batch_size = 1
seq_length = 512
data = torch.randint(vocab_size, size=[batch_size, seq_length])

import intel_extension_for_pytorch as ipex
model = ipex.optimize(model, dtype=torch.bfloat16)

with torch.no_grad():
  with torch.cpu.amp.autocast():
    d = torch.randint(vocab_size, size=[batch_size, seq_length])
    model = torch.jit.trace(model, (d,), check_trace=False, strict=False)
    model = torch.jit.freeze(model)

    model(data)
```

### INT8

#### Calibration

```
import os
import torch

model = Model()
model.eval()
data = torch.rand(<shape>)

import intel_extension_for_pytorch as ipex
# For first-time calibration, pass dtype into the QuantConf function to generate a conf
if os.path.isfile('int8_conf.json'):
  conf = ipex.QuantConf(dtype=torch.int8) 
# For re-calibration, pass the generated json file into the QuantConf function to generate a conf
else:
  conf = ipex.QuantConf('int8_conf.json')
model, conf = ipex.quantization.prepare(model, conf)
for d in calibration_data_loader(): 
  # conf will be updated with observed statistics during calibrating with the dataset 
  with ipex.quantization.calibrate(conf):
    model(d) 
conf.save('int8_conf.json', default_recipe=True)
model = ipex.quantization.convert(model, conf, torch.rand(<shape>)) 
```

#### Deployment

##### Imperative Mode

```
import torch

model = models.Model()
model.eval()
data = torch.rand(<shape>)

import intel_extension_for_pytorch as ipex
conf = ipex.QuantConf('int8_conf.json')
model = ipex.quantization.convert(model, conf, torch.rand(<shape>)) 

with torch.no_grad():
  model(data)
```

##### Graph Mode

```
import torch
import intel_extension_for_pytorch as ipex

model = torch.jit.load('<INT8 model file>')
model.eval()
data = torch.rand(<shape>)

with torch.no_grad():
  model(data)
```

## C++

To work with libtorch, C++ library of PyTorch, Intel® Extension for PyTorch\* provides its C++ dynamic library as well. The C++ library is supposed to handle inference workload only, such as service deployment. For regular development, please use Python interface. Comparing to usage of libtorch, no specific code changes are required, except for converting input data into channels last data format. Compilation follows the recommended methodology with CMake. Detailed instructions can be found in [PyTorch tutorial](https://pytorch.org/tutorials/advanced/cpp_export.html#depending-on-libtorch-and-building-the-application).

During compilation, Intel optimizations will be activated automatically once C++ dynamic library of Intel® Extension for PyTorch\* is linked.

The example code below works for all data types.

**example-app.cpp**

```cpp
#include <torch/script.h>
#include <iostream>
#include <memory>

int main(int argc, const char* argv[]) {
    torch::jit::script::Module module;
    try {
        module = torch::jit::load(argv[1]);
    }
    catch (const c10::Error& e) {
        std::cerr << "error loading the model\n";
        return -1;
    }
    std::vector<torch::jit::IValue> inputs;
    // make sure input data are converted to channels last format
    inputs.push_back(torch::ones({1, 3, 224, 224}).to(c10::MemoryFormat::ChannelsLast));

    at::Tensor output = module.forward(inputs).toTensor();

    return 0;
}
```

**CMakeList.txt**

```cmake
cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(example-app)

find_package(intel-ext-pt-cpu REQUIRED)

add_executable(example-app example-app.cpp)
target_link_libraries(example-app "${TORCH_LIBRARIES}")

set_property(TARGET example-app PROPERTY CXX_STANDARD 14)
```

**Command for compilation**

```bash
$ cmake -DCMAKE_PREFIX_PATH=<LIBPYTORCH_PATH> ..
$ make
```

If *Found INTEL_EXT_PT_CPU* is shown as *TRUE*, the extension had been linked into the binary. This can be verified with Linux command *ldd*.

```bash
$ cmake -DCMAKE_PREFIX_PATH=/workspace/libtorch ..
-- The C compiler identification is GNU 9.3.0
-- The CXX compiler identification is GNU 9.3.0
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Looking for pthread.h
-- Looking for pthread.h - found
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Failed
-- Looking for pthread_create in pthreads
-- Looking for pthread_create in pthreads - not found
-- Looking for pthread_create in pthread
-- Looking for pthread_create in pthread - found
-- Found Threads: TRUE
-- Found Torch: /workspace/libtorch/lib/libtorch.so
-- Found INTEL_EXT_PT_CPU: TRUE
-- Configuring done
-- Generating done
-- Build files have been written to: /workspace/build

$ ldd example-app
        ...
        libtorch.so => /workspace/libtorch/lib/libtorch.so (0x00007f3cf98e0000)
        libc10.so => /workspace/libtorch/lib/libc10.so (0x00007f3cf985a000)
        libintel-ext-pt-cpu.so => /workspace/libtorch/lib/libintel-ext-pt-cpu.so (0x00007f3cf70fc000)
        libtorch_cpu.so => /workspace/libtorch/lib/libtorch_cpu.so (0x00007f3ce16ac000)
        ...
        libdnnl_graph.so.0 => /workspace/libtorch/lib/libdnnl_graph.so.0 (0x00007f3cde954000)
        ...
```

## Model Zoo

Use cases that had already been optimized by Intel engineers are available at [Model Zoo for Intel® Architecture](https://github.com/IntelAI/models/tree/pytorch-r1.10-models). A bunch of PyTorch use cases for benchmarking are also available on the [Github page](https://github.com/IntelAI/models/tree/pytorch-r1.10-models/benchmarks#pytorch-use-cases). You can get performance benefits out-of-box by simply running scipts in the Model Zoo.
