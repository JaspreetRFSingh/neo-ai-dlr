#include "dlr_tvm.h"
#include <fstream>
#include <numeric>


using namespace dlr;

ModelPath dlr::GetTvmPaths(const std::string& dirname) {
  ModelPath paths;
  std::vector<std::string> paths_vec;
  ListDir(dirname, paths_vec);
  for (auto filename : paths_vec) {
    std::string basename = GetBasename(filename);
    if (EndsWith(filename, ".json")
        && std::all_of(std::begin(SAGEMAKER_AUXILIARY_JSON_FILES),
                       std::end(SAGEMAKER_AUXILIARY_JSON_FILES),
                       [basename](const std::string& s)
                                 { return (s != basename); })
        && filename != "version.json") {
      paths.model_json = filename;
    } else if (EndsWith(filename, LIBEXT)) {
      paths.model_lib = filename;
    } else if (EndsWith(filename, ".params")) {
      paths.params = filename;
    } else if (filename == "version.json") {
      paths.ver_json = filename;
    }
  }
  if ( paths.model_json.empty() || paths.model_lib.empty() || paths.params.empty() ){
    LOG(FATAL) << "No valid TVM model files found under folder:" << dirname;
  }
  return paths;
}

bool IsFileEmpty(const std::string &filePath){
  std::ifstream pFile(filePath);
  return pFile.peek() == std::ifstream::traits_type::eof();
}

void TVMModel::SetupTVMModule(const std::string& model_path) {
  ModelPath paths = GetTvmPaths(model_path);
  std::ifstream jstream(paths.model_json);
  std::stringstream json_blob;
  json_blob << jstream.rdbuf();
  std::ifstream pstream(paths.params);
  std::stringstream param_blob;
  param_blob << pstream.rdbuf();

  tvm::runtime::Module module;
  if (!IsFileEmpty(paths.model_lib)){
    module = tvm::runtime::Module::LoadFromFile(paths.model_lib);
  }
  tvm_graph_runtime_ =
    std::make_shared<tvm::runtime::GraphRuntime>();
  tvm_graph_runtime_->Init(json_blob.str(), module, {ctx_});
  tvm_graph_runtime_->LoadParams(param_blob.str());

  tvm_module_ = std::make_shared<tvm::runtime::Module>(
      tvm::runtime::Module(tvm_graph_runtime_));

  // This is the combined count of inputs and weights
  const auto num_inputs_weights = tvm_graph_runtime_->NumInputs();
  std::vector<std::string> input_names;
  for (int i = 0; i < num_inputs_weights; i++)  {
    input_names.push_back(tvm_graph_runtime_->GetInputName(i));
  }
  // Get list of weights
  weight_names_ = tvm_graph_runtime_->GetWeightNames();
  num_weights_ = weight_names_.size();
  // tvm_graph_runtime_->GetInputName(*) returns both inputs and weights
  // Compute set difference to get names of inputs only
  std::sort(input_names.begin(), input_names.end());
  std::sort(weight_names_.begin(), weight_names_.end());
  std::set_difference(input_names.begin(), input_names.end(),
                      weight_names_.begin(), weight_names_.end(),
                      std::inserter(input_names_, input_names_.begin()));
  // Save the number of inputs
  num_inputs_ = input_names_.size();

  // Get the number of output and reserve space to save output tensor
  // pointers.
  num_outputs_ = tvm_graph_runtime_->NumOutputs();
  outputs_.resize(num_outputs_);
  for (int i = 0; i < num_outputs_; i++) {
    tvm::runtime::NDArray output = tvm_graph_runtime_->GetOutput(i);
    outputs_[i] = output.operator->();
  }
}

std::vector<std::string> TVMModel::GetWeightNames() const {
  return tvm_graph_runtime_->GetWeightNames();
}

const char* TVMModel::GetInputName(int index) const {
  CHECK_LT(index, num_inputs_) << "Input index is out of range.";
  return input_names_[index].c_str();
}

const char* TVMModel::GetWeightName(int index) const {
  CHECK_LT(index, num_weights_) << "Weight index is out of range.";
  return weight_names_[index].c_str();
}

void TVMModel::SetInput(const char* name, const int64_t* shape, float* input,
                        int dim) {
    std::string str(name);
    int index = tvm_graph_runtime_->GetInputIndex(str);
    tvm::runtime::NDArray arr = tvm_graph_runtime_->GetInput(index);
    DLTensor input_tensor = *(arr.operator->());
    input_tensor.ctx = DLContext{kDLCPU, 0};
    input_tensor.data = input;
    int64_t read_size =
        std::accumulate(shape, shape + dim, 1, std::multiplies<int64_t>());
    int64_t expected_size = std::accumulate(
        input_tensor.shape, input_tensor.shape + input_tensor.ndim, 1,
        std::multiplies<int64_t>());
    CHECK_SHAPE("Mismatch found in input data size", read_size,
                expected_size);
    tvm::runtime::PackedFunc set_input = tvm_module_->GetFunction("set_input");
    set_input(str, &input_tensor);
}

void TVMModel::GetInput(const char* name, float* input) {
  std::string str(name);
  int index = tvm_graph_runtime_->GetInputIndex(str);
  tvm::runtime::NDArray arr = tvm_graph_runtime_->GetInput(index);
  DLTensor input_tensor;
  input_tensor.data = input;
  input_tensor.ctx = DLContext{kDLCPU, 0};
  input_tensor.ndim = arr->ndim;
  input_tensor.dtype = arr->dtype;
  input_tensor.shape = arr->shape;
  input_tensor.strides = nullptr;
  input_tensor.byte_offset = 0;
  arr.CopyTo(&input_tensor);
}

void TVMModel::GetOutputShape(int index, int64_t* shape) const {
  std::memcpy(shape, outputs_[index]->shape,
              sizeof(int64_t) * outputs_[index]->ndim);
}

void TVMModel::GetOutput(int index, float* out) {
  DLTensor output_tensor = *outputs_[index];
  output_tensor.ctx = DLContext{kDLCPU, 0};
  output_tensor.data = out;
  tvm::runtime::PackedFunc get_output = tvm_module_->GetFunction("get_output");
  get_output(index, &output_tensor);
}

void TVMModel::GetOutputSizeDim(int index, int64_t* size, int* dim) {
  *size = 1;
  const DLTensor* tensor = outputs_[index];
  for (int i = 0; i < tensor->ndim; ++i) {
    *size *= tensor->shape[i];
  }
  *dim = tensor->ndim;
}

void TVMModel::Run() {
  tvm::runtime::PackedFunc run = tvm_module_->GetFunction("run");
  run();
}

const char* TVMModel::GetBackend() const {
  return "tvm";
}
