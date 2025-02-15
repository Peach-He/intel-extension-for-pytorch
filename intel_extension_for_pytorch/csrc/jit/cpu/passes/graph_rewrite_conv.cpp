#include "graph_rewrite.h"
#include "graph_rewrite_utils.h"

#include <torch/csrc/jit/frontend/code_template.h>

namespace torch {
namespace jit {
namespace graph_rewrite {

void insertPrePackedConv2dOp(Block* b) {
  for (Node* n : b->nodes()) {
    for (Block* block : n->blocks()) {
      insertPrePackedConv2dOp(block);
    }
    // TODO: add conv3d
    if (n->kind() == aten::conv2d ||
        n->kind() ==
            Symbol::fromQualString("torch_ipex::convolution_forward")) {
      WithInsertPoint guard(n);
      auto graph = n->owningGraph();
      auto prepack_node = graph->create(
          Symbol::fromQualString("ipex_prepack::convolution_prepack"), 1);
      auto input_size_option = n->inputs()
                                   .at(0)
                                   ->type()
                                   ->cast<TensorType>()
                                   ->sizes()
                                   .concrete_sizes();
      // if can't get input shape info, will not do weight prepack.
      if (!(input_size_option.has_value() &&
            input_size_option.value().size() == 4)) {
        continue;
      }
      IValue input_size_value(input_size_option.value());
      if (n->kind() == aten::conv2d) {
        auto weight_size_option = n->inputs()
                                      .at(1)
                                      ->type()
                                      ->cast<TensorType>()
                                      ->sizes()
                                      .concrete_sizes();
        // weight has not shape info, will not do weight prapacked.
        if (!(weight_size_option.has_value() &&
              weight_size_option.value().size() == 4)) {
          continue;
        }
        auto weight_size = weight_size_option.value();
        std::vector<int64_t> k_size = {weight_size[2], weight_size[3]};
        // w_is_channels_last is invaild, there will has a check the memory
        // format at convolution kernel side.
        bool w_is_channels_last = false;
        int64_t o_channel = weight_size[0];
        IValue kernel_size_value(k_size), weight_is_prepacked_value(false),
            weight_is_channels_last_value(w_is_channels_last),
            output_channel_value(o_channel);

        auto kernel_size = graph->insertConstant(kernel_size_value);
        auto weight_is_prepacked =
            graph->insertConstant(weight_is_prepacked_value);
        auto weight_is_channels_last =
            graph->insertConstant(weight_is_channels_last_value);
        auto output_channel = graph->insertConstant(output_channel_value);

        for (auto i = 1; i < n->inputs().size() - 1; ++i) {
          Value* v = n->inputs().at(i);
          prepack_node->addInput(v);
        }
        prepack_node->addInput(kernel_size);
        // add conv groups
        prepack_node->addInput(n->inputs().at(n->inputs().size() - 1));
        prepack_node->addInput(output_channel);
        prepack_node->addInput(weight_is_channels_last);
        prepack_node->addInput(weight_is_prepacked);
      } else {
        for (auto i = 1; i < n->inputs().size(); ++i) {
          Value* v = n->inputs().at(i);
          prepack_node->addInput(v);
        }
      }
      auto input_size = graph->insertConstant(input_size_value);
      prepack_node->addInput(input_size);
      prepack_node->output()->setType(getCustomClass(
          "__torch__.torch.classes.ipex_prepack.ConvolutionOpContext"));

      graph->insertNode(prepack_node);
      auto prepack_conv = graph->insertNode(graph->create(
          Symbol::fromQualString("ipex_prepack::convolution_run"), 1));
      prepack_conv->addInput(n->inputs().at(0));
      prepack_conv->addInput(prepack_node->output());
      prepack_conv->output()->setType(n->output()->type()->cast<TensorType>());
      auto v = n->outputs().at(0);
      n->output()->replaceAllUsesWith(prepack_conv->output());
    }
  }
  EliminateDeadCode(b);
}

void insertPrePackedConv2dOp(std::shared_ptr<Graph>& graph) {
  insertPrePackedConv2dOp(graph->block());
}

void fuseConvWithEltwise(std::shared_ptr<Graph>& graph) {
  IpexSubgraphRewriter rewriter_relu, rewriter_sigmoid, rewriter_hardtanh,
      rewriter_elu, rewriter_swish, rewriter_silu;
  std::array<std::string, 2> relu_operators = {"relu", "relu_"};
  std::array<std::string, 2> sigmoid_operators = {"sigmoid", "sigmoid_"};
  std::array<std::string, 2> hardtanh_operators = {"hardtanh", "hardtanh_"};
  std::array<std::string, 2> elu_operators = {"elu", "elu_"};
  std::array<std::string, 2> mul_operators = {"mul", "mul_"};
  std::array<std::string, 2> silu_operators = {"silu", "silu_"};

  auto conv_relu_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked,  %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${relu}(%x)
        return (%res))");

  std::string conv_relu_fused = R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_relu_run(%input, %packed_weight)
        return (%res))";

  auto conv_sigmoid_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${sigmoid}(%x)
        return (%res))");

  std::string conv_sigmoid_fused = R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_sigmoid_run(%input, %packed_weight)
        return (%res))";

  auto conv_hardtanh_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[], %min, %max):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${hardtanh}(%x, %min, %max)
        return (%res))");

  std::string conv_hardtanh_fused = R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[], %min, %max):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_hardtanh_run(%input, %min, %max, %packed_weight)
        return (%res))";

  auto conv_elu_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[], %alpha, %scale, %input_scale):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel,  %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${elu}(%x, %alpha, %scale, %input_scale)
        return (%res))");

  std::string conv_elu_fused = R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[], %alpha, %scale, %input_scale):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel,  %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_elu_run(%input, %alpha, %scale, %input_scale, %packed_weight)
        return (%res))";

  auto conv_sigmoid_mul_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %y = aten::${sigmoid}(%x)
        %res = aten::${mul}(%x, %y)
        return (%res))");

  auto conv_silu_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${silu}(%x)
        return (%res))");

  std::string conv_swish_fused = R"(
    graph(%input, %weight, %bias, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel,  %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_swish_run(%input, %packed_weight)
        return (%res))";

  for (const auto& relu : relu_operators) {
    TemplateEnv env;
    env.s("relu", relu);
    rewriter_relu.RegisterRewritePattern(
        conv_relu_rstring.format(env), conv_relu_fused);
  }

  for (const auto& sigmoid : sigmoid_operators) {
    TemplateEnv env;
    env.s("sigmoid", sigmoid);
    rewriter_sigmoid.RegisterRewritePattern(
        conv_sigmoid_rstring.format(env), conv_sigmoid_fused);
    for (const auto& mul : mul_operators) {
      env.s("mul", mul);
      rewriter_swish.RegisterRewritePattern(
          conv_sigmoid_mul_rstring.format(env), conv_swish_fused);
    }
  }
  for (const auto& silu : silu_operators) {
    TemplateEnv env;
    env.s("silu", silu);
    rewriter_silu.RegisterRewritePattern(
        conv_silu_rstring.format(env), conv_swish_fused);
  }

  for (const auto& hardtanh : hardtanh_operators) {
    TemplateEnv env;
    env.s("hardtanh", hardtanh);
    rewriter_hardtanh.RegisterRewritePattern(
        conv_hardtanh_rstring.format(env), conv_hardtanh_fused);
  }

  for (const auto& elu : elu_operators) {
    TemplateEnv env;
    env.s("elu", elu);
    rewriter_elu.RegisterRewritePattern(
        conv_elu_rstring.format(env), conv_elu_fused);
  }

  auto filter_conv2d_elu =
      [](const Match& match,
         const std::unordered_map<std::string, Value*>& vmap) {
        const auto& match_vmap = match.values_map;
        auto input_scale_value =
            getIValue("input_scale", match_vmap, vmap).value();
        bool no_input_scale = input_scale_value.isDouble()
            ? (input_scale_value.toDouble() == 1.0)
            : (input_scale_value.toInt() == 1);
        return no_input_scale;
      };

  rewriter_relu.runOnGraph(graph);
  rewriter_sigmoid.runOnGraph(graph);
  rewriter_hardtanh.runOnGraph(graph);
  rewriter_elu.runOnGraph(graph, filter_conv2d_elu);
  rewriter_swish.runOnGraph(graph);
  rewriter_silu.runOnGraph(graph);
}

void fuseConvAddRelu(std::shared_ptr<Graph>& graph) {
  IpexSubgraphRewriter rewriter_add_v1, rewriter_add_v2, rewriter_add_relu;
  std::array<std::string, 2> add_operators = {"add", "add_"};
  std::array<std::string, 2> relu_operators = {"relu", "relu_"};

  // conv   Y
  //   \   /
  //    add
  // output = conv_output + alpha*Y
  auto conv_add_rstring_v1 = CodeTemplate(R"(
    graph(%input, %weight, %bias, %accumu, %alpha, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${add}(%x, %accumu, %alpha) return (%res))");

  //  Y     conv
  //   \   /
  //    add
  // output = Y + alpha*conv_output, alpha need to one or none.
  auto conv_add_rstring_v2 = CodeTemplate(R"(
    graph(%input, %weight, %bias, %accumu, %alpha, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel,  %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_run(%input, %packed_weight)
        %res = aten::${add}(%accumu, %x, %alpha) return (%res))");

  std::string conv_add_fused = R"(
    graph(%input, %weight, %bias, %accumu, %alpha, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_add_run(%input, %accumu, %alpha, %packed_weight)
        return (%res))";

  auto conv_add_relu_rstring = CodeTemplate(R"(
    graph(%input, %weight, %bias, %accumu, %alpha, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %x = ipex_prepack::convolution_add_run(%input, %accumu, %alpha, %packed_weight)
        %res = aten::${relu}(%x) return (%res))");

  std::string conv_add_relu_fused = R"(
    graph(%input, %weight, %bias, %accumu, %alpha, %stride:int[], %padding:int[], %dilation:int[], %kernel_size:int[], %groups:int, %output_channel:int, %weight_is_channels_last:bool, %weight_is_prepacked:bool, %input_size:int[]):
        %packed_weight = ipex_prepack::convolution_prepack(%weight, %bias, %stride, %padding, %dilation, %kernel_size, %groups, %output_channel, %weight_is_channels_last, %weight_is_prepacked, %input_size)
        %res = ipex_prepack::convolution_add_relu_run(%input, %accumu, %alpha, %packed_weight)
        return (%res))";

  // conv+add
  for (const auto& add : add_operators) {
    TemplateEnv env;
    env.s("add", add);
    rewriter_add_v1.RegisterRewritePattern(
        conv_add_rstring_v1.format(env), conv_add_fused);
    rewriter_add_v2.RegisterRewritePattern(
        conv_add_rstring_v2.format(env), conv_add_fused);
  }

  // fused_conv_add+relu
  for (const auto& relu : relu_operators) {
    TemplateEnv env;
    env.s("relu", relu);
    rewriter_add_relu.RegisterRewritePattern(
        conv_add_relu_rstring.format(env), conv_add_relu_fused);
  }

  rewriter_add_v1.runOnGraph(graph, fuse_add_filter_v1);
  rewriter_add_v2.runOnGraph(graph, fuse_add_filter_v2);
  rewriter_add_relu.runOnGraph(graph);
}

} // namespace graph_rewrite
} // namespace jit
} // namespace torch
