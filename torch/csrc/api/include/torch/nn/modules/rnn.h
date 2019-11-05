#pragma once

#include <torch/nn/cloneable.h>
#include <torch/nn/options/rnn.h>
#include <torch/nn/modules/dropout.h>
#include <torch/nn/pimpl.h>
#include <torch/types.h>

#include <ATen/ATen.h>
#include <c10/util/Exception.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace torch {
namespace nn {

/// The output of a single invocation of an RNN module's `forward()` method.
struct TORCH_API RNNOutput {
  /// The result of applying the specific RNN algorithm
  /// to the input tensor and input state.
  Tensor output;
  /// The new, updated state that can be fed into the RNN
  /// in the next forward step.
  Tensor state;
};

namespace detail {
/// Base class for all RNN implementations (intended for code sharing).
template <typename Derived>
class TORCH_API RNNImplBase : public torch::nn::Cloneable<Derived> {
 public:
  /// These must line up with the CUDNN mode codes:
  /// https://docs.nvidia.com/deeplearning/sdk/cudnn-developer-guide/index.html#cudnnRNNMode_t
  enum class CuDNNMode { RNN_RELU = 0, RNN_TANH = 1, LSTM = 2, GRU = 3 };

  template <typename V>
  CuDNNMode cudnnmode_get_enum(V variant_enum) {
    if (c10::get_if<enumtype::kReLU>(&variant_enum)) {
      return CuDNNMode::RNN_RELU;
    } else if (c10::get_if<enumtype::kTanh>(&variant_enum)) {
      return CuDNNMode::RNN_RELU;
    } else {
      TORCH_CHECK(
      false,
      get_enum_name(variant_enum), " is not a valid value for CuDNNMode");
    }
  }

  explicit RNNImplBase(
      const RNNOptionsBase& options_,
      optional<CuDNNMode> cudnn_mode = nullopt,
      int64_t number_of_gates = 1);

  /// Initializes the parameters of the RNN module.
  void reset() override;

  /// Overrides `nn::Module::to()` to call `flatten_parameters()` after the
  /// original operation.
  void to(torch::Device device, torch::Dtype dtype, bool non_blocking = false)
      override;
  void to(torch::Dtype dtype, bool non_blocking = false) override;
  void to(torch::Device device, bool non_blocking = false) override;

  /// Pretty prints the RNN module into the given `stream`.
  void pretty_print(std::ostream& stream) const override;

  /// Modifies the internal storage of weights for optimization purposes.
  ///
  /// On CPU, this method should be called if any of the weight or bias vectors
  /// are changed (i.e. weights are added or removed). On GPU, it should be
  /// called __any time the storage of any parameter is modified__, e.g. any
  /// time a parameter is assigned a new value. This allows using the fast path
  /// in cuDNN implementations of respective RNN `forward()` methods. It is
  /// called once upon construction, inside `reset()`.
  void flatten_parameters();

  /// The RNN's options.
  RNNOptionsBase options;

  /// The weights for `input x hidden` gates.
  std::vector<Tensor> w_ih;
  /// The weights for `hidden x hidden` gates.
  std::vector<Tensor> w_hh;
  /// The biases for `input x hidden` gates.
  std::vector<Tensor> b_ih;
  /// The biases for `hidden x hidden` gates.
  std::vector<Tensor> b_hh;

 protected:
  /// The function signature of `rnn_relu`, `rnn_tanh` and `gru`.
  using RNNFunctionSignature = std::tuple<Tensor, Tensor>(
      /*input=*/const Tensor&,
      /*state=*/const Tensor&,
      /*params=*/TensorList,
      /*has_biases=*/bool,
      /*layers=*/int64_t,
      /*dropout=*/double,
      /*train=*/bool,
      /*bidirectional=*/bool,
      /*batch_first=*/bool);

  /// A generic `forward()` used for RNN and GRU (but not LSTM!). Takes the ATen
  /// RNN function as first argument.
  RNNOutput generic_forward(
      std::function<RNNFunctionSignature> function,
      const Tensor& input,
      Tensor state);

  /// Returns a flat vector of all weights, with layer weights following each
  /// other sequentially in (w_ih, w_hh, b_ih, b_hh) order.
  std::vector<Tensor> flat_weights() const;

  /// Very simple check if any of the parameters (weights, biases) are the same.
  bool any_parameters_alias() const;

  /// The number of gate weights/biases required by the RNN subclass.
  int64_t number_of_gates_;

  /// The cuDNN RNN mode, if this RNN subclass has any.
  optional<CuDNNMode> cudnn_mode_;

  /// The cached result of the latest `flat_weights()` call.
  std::vector<Tensor> flat_weights_;
};
} // namespace detail

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RNN ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/// A multi-layer Elman RNN module with Tanh or ReLU activation.
/// See https://pytorch.org/docs/master/nn.html#torch.nn.RNN to learn about the
/// exact behavior of this module.
class TORCH_API RNNImpl : public detail::RNNImplBase<RNNImpl> {
 public:
  RNNImpl(int64_t input_size, int64_t hidden_size)
      : RNNImpl(RNNOptions(input_size, hidden_size)) {}
  explicit RNNImpl(const RNNOptions& options_);

  /// Pretty prints the `RNN` module into the given `stream`.
  void pretty_print(std::ostream& stream) const override;

  /// Applies the `RNN` module to an input sequence and input state.
  /// The `input` should follow a `(sequence, batch, features)` layout unless
  /// `batch_first` is true, in which case the layout should be `(batch,
  /// sequence, features)`.
  RNNOutput forward(const Tensor& input, Tensor state = {});

  RNNOptions options;
};

/// A `ModuleHolder` subclass for `RNNImpl`.
/// See the documentation for `RNNImpl` class to learn what methods it provides,
/// or the documentation for `ModuleHolder` to learn about PyTorch's module
/// storage semantics.
TORCH_MODULE(RNN);

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LSTM ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/// A multi-layer long-short-term-memory (LSTM) module.
/// See https://pytorch.org/docs/master/nn.html#torch.nn.LSTM to learn about the
/// exact behavior of this module.
class TORCH_API LSTMImpl : public detail::RNNImplBase<LSTMImpl> {
 public:
  LSTMImpl(int64_t input_size, int64_t hidden_size)
      : LSTMImpl(LSTMOptions(input_size, hidden_size)) {}
  explicit LSTMImpl(const LSTMOptions& options_);

  /// Applies the `LSTM` module to an input sequence and input state.
  /// The `input` should follow a `(sequence, batch, features)` layout unless
  /// `batch_first` is true, in which case the layout should be `(batch,
  /// sequence, features)`.
  RNNOutput forward(const Tensor& input, Tensor state = {});
};

/// A `ModuleHolder` subclass for `LSTMImpl`.
/// See the documentation for `LSTMImpl` class to learn what methods it
/// provides, or the documentation for `ModuleHolder` to learn about PyTorch's
/// module storage semantics.
TORCH_MODULE(LSTM);

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ GRU ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/// A multi-layer gated recurrent unit (GRU) module.
/// See https://pytorch.org/docs/master/nn.html#torch.nn.GRU to learn about the
/// exact behavior of this module.
class TORCH_API GRUImpl : public detail::RNNImplBase<GRUImpl> {
 public:
  GRUImpl(int64_t input_size, int64_t hidden_size)
      : GRUImpl(GRUOptions(input_size, hidden_size)) {}
  explicit GRUImpl(const GRUOptions& options_);

  /// Applies the `GRU` module to an input sequence and input state.
  /// The `input` should follow a `(sequence, batch, features)` layout unless
  /// `batch_first` is true, in which case the layout should be `(batch,
  /// sequence, features)`.
  RNNOutput forward(const Tensor& input, Tensor state = {});
};

/// A `ModuleHolder` subclass for `GRUImpl`.
/// See the documentation for `GRUImpl` class to learn what methods it provides,
/// or the documentation for `ModuleHolder` to learn about PyTorch's module
/// storage semantics.
TORCH_MODULE(GRU);

} // namespace nn
} // namespace torch
