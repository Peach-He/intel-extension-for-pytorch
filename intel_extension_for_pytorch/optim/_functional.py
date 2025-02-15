r"""Functional interface, port from torch/optim/_function.py"""
import torch
from torch import Tensor
from typing import List, Optional

def _make_sparse(grad, grad_indices, values):
    size = grad.size()
    if grad_indices.numel() == 0 or values.numel() == 0:
        return torch.empty_like(grad)
    return torch.sparse_coo_tensor(grad_indices, values, size)

def _adagrad_impl(
    params: List[Tensor],
    grads: List[Tensor],
    state_sums: List[Tensor],
    state_steps: List[int],
    attr: dict,
    lr: float,
    weight_decay: float,
    lr_decay: float,
    eps: float,
    fused: bool):
    r"""Functional API that performs Adagrad algorithm computation.

    See :class:`~torch.optim.Adagrad` for details.
    """

    for (param, grad, state_sum, step) in zip(params, grads, state_sums, state_steps):
        param2 = torch.Tensor()
        if param in attr:
            if 'trail' in attr[param]:
                assert param.dtype is torch.bfloat16
                param2 = attr[param]['trail']
            if 'bf16_param' in attr[param]:
                assert param.dtype is torch.float
                param2 = attr[param][bf16_param]
        if fused and not param.is_sparse:
            torch.ops.torch_ipex.adagrad_fused_step(
                param,
                grad,
                state_sum,
                param2,
                step,
                lr,
                weight_decay,
                lr_decay,
                eps)
            continue

        if weight_decay != 0:
            if grad.is_sparse:
                raise RuntimeError("weight_decay option is not compatible with sparse gradients")
            grad = grad.add(param, alpha=weight_decay)

        clr = lr / (1 + (step - 1) * lr_decay)

        if grad.is_sparse:
            grad = grad.coalesce()  # the update is non-linear so indices must be unique
            grad_indices = grad._indices()
            grad_values = grad._values()
            size = grad.size()

            state_sum.add_(_make_sparse(grad, grad_indices, grad_values.pow(2)))
            std = state_sum.sparse_mask(grad)
            std_values = std._values().sqrt_().add_(eps)
            param.add_(_make_sparse(grad, grad_indices, grad_values / std_values), alpha=-clr)
        else:
            state_sum.addcmul_(grad, grad, value=1)
            std = state_sum.sqrt().add_(eps)
            param.addcdiv_(grad, std, value=-clr)

@torch.no_grad()
def adagrad_step(self, closure=None):
    """Performs a single optimization step.

    Args:
        closure (callable, optional): A closure that reevaluates the model
            and returns the loss.
    """
    loss = None
    if closure is not None:
        with torch.enable_grad():
            loss = closure()

    for group in self.param_groups:
        params_with_grad = []
        grads = []
        state_sums = []
        state_steps = []

        for p in group['params']:
            if p.grad is not None:
                params_with_grad.append(p)
                grads.append(p.grad)
                state = self.state[p]
                state_sums.append(state['sum'])
                # update the steps for each param group update
                state['step'] += 1
                # record the step after step update
                state_steps.append(state['step'])

        _adagrad_impl(
            params_with_grad,
            grads,
            state_sums,
            state_steps,
            self.params_attr,
            group['lr'],
            group['weight_decay'],
            group['lr_decay'],
            group['eps'],
            self.fused)

    return loss

def _sgd_impl(
    params: List[Tensor],
    d_p_list: List[Tensor],
    attr: dict,
    momentum_buffer_list: List[Optional[Tensor]],
    *,
    weight_decay: float,
    momentum: float,
    lr: float,
    dampening: float,
    nesterov: bool,
    fused: bool):
    r"""Functional API that performs SGD algorithm computation.

    See :class:`~torch.optim.SGD` for details.
    """

    for i, param in enumerate(params):
        d_p = d_p_list[i]
        param2 = torch.Tensor()
        if param in attr:
            if 'trail' in attr[param]:
                assert param.dtype is torch.bfloat16
                param2 = attr[param]['trail']
            if 'bf16_param' in attr[param]:
                assert param.dtype is torch.float
                param2 = attr[param][bf16_param]

        # first iter will init momentum_buffer, not fused on 1st iter
        if fused and not param.is_sparse and momentum_buffer_list[i] is not None:
            torch.ops.torch_ipex.sgd_fused_step(
                param,
                d_p,
                momentum_buffer_list[i],
                param2,
                momentum,
                lr,
                weight_decay,
                dampening,
                nesterov)
            continue

        float_d_p, float_param = None, None
        if weight_decay != 0 or momentum != 0:
            float_d_p = d_p.float()
            if param.dtype == torch.bfloat16:
                float_param = torch.ops.torch_ipex.cat_bfloat16_float(param, param2)
            else:
                float_param = param.float()

        if weight_decay != 0:
            float_d_p = float_d_p.add(float_param, alpha=weight_decay)

        if momentum != 0:
            buf = momentum_buffer_list[i]
            if buf is None:
                buf = torch.clone(float_d_p).detach()
                momentum_buffer_list[i] = buf
            else:
                buf.mul_(momentum).add_(float_d_p, alpha=1 - dampening)

            if nesterov:
                float_d_p = d_p.add(buf, alpha=momentum)
            else:
                float_d_p = buf

        if param.dtype is torch.bfloat16:
            if float_d_p is not None and float_param is not None:
                float_param.add_(float_d_p, alpha=-lr)
                top_half, bot_half = torch.ops.torch_ipex.split_float_bfloat16(float_param)
                param.copy_(top_half)
                param2.copy_(bot_half)
            else:
                torch.ops.torch_ipex.packed_add(param, param2, d_p, alpha=-lr)
        else:
            if float_d_p is not None:
                param.add_(float_d_p, alpha=-lr)
            else:
                param.add_(d_p, alpha=-lr)

@torch.no_grad()
def sgd_step(self, closure=None):
    """Performs a single optimization step.

    Args:
        closure (callable, optional): A closure that reevaluates the model
            and returns the loss.
    """
    loss = None
    if closure is not None:
        with torch.enable_grad():
            loss = closure()

    for group in self.param_groups:
        params_with_grad = []
        d_p_list = []
        momentum_buffer_list = []
        weight_decay = group['weight_decay']
        momentum = group['momentum']
        dampening = group['dampening']
        nesterov = group['nesterov']
        lr = group['lr']

        for p in group['params']:
            if p.grad is not None:
                params_with_grad.append(p)
                d_p_list.append(p.grad)

                state = self.state[p]
                if 'momentum_buffer' not in state:
                    momentum_buffer_list.append(None)
                else:
                    momentum_buffer_list.append(state['momentum_buffer'])

        _sgd_impl(
            params_with_grad,
            d_p_list,
            self.params_attr,
            momentum_buffer_list,
            weight_decay=weight_decay,
            momentum=momentum,
            lr=lr,
            dampening=dampening,
            nesterov=nesterov,
            fused=self.fused)

        # update momentum_buffers in state
        for p, momentum_buffer in zip(params_with_grad, momentum_buffer_list):
            state = self.state[p]
            state['momentum_buffer'] = momentum_buffer

    return loss

def lamb_impl(
    params: List[Tensor],
    grads: List[Tensor],
    exp_avgs: List[Tensor],
    exp_avg_sqs: List[Tensor],
    attr: dict,
    state_steps: List[int],
    beta1: float,
    beta2: float,
    lr: float,
    weight_decay: float,
    eps: float,
    fused: bool):

    r"""Functional API that performs Lamb algorithm computation.
    See :class:`~torch.optim.Lamb` for details.
    """

    for i, param in enumerate(params):

        grad = grads[i]
        exp_avg = exp_avgs[i]
        exp_avg_sq = exp_avg_sqs[i]
        step = state_steps[i]
        param2 = torch.Tensor()
        if param in attr:
            if 'trail' in attr[param]:
                assert param.dtype is torch.bfloat16
                param2 = attr[param]['trail']
            if 'bf16_param' in attr[param]:
                assert param.dtype is torch.float
                param2 = attr[param][bf16_param]
        if fused:
            torch.ops.torch_ipex.lamb_fused_step(
                param,
                exp_avg,
                exp_avg_sq,
                grad,
                param2,
                step,
                beta1,
                beta2,
                lr,
                weight_decay,
                eps)
            continue

        bias_correction1 = 1 - beta1 ** step
        bias_correction2 = 1 - beta2 ** step

        # Decay the first and second moment running average coefficient
        exp_avg.mul_(beta1).add_(grad, alpha=1 - beta1)
        exp_avg_sq.mul_(beta2).addcmul_(grad, grad, value=1 - beta2)

        adam_step = (exp_avg / bias_correction1) / ((exp_avg_sq / bias_correction2).sqrt() + eps)

        if weight_decay != 0:
            adam_step.add_(param, alpha=weight_decay)

        weight_norm = param.norm(p=2)
        rtw_norm = adam_step.norm(p=2)
        true_ratio = weight_norm / rtw_norm

        param.add_(adam_step, alpha=-lr * true_ratio)
