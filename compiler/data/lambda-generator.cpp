#include <functional>
#include <algorithm>

#include "compiler/compiler-core.h"
#include "compiler/data/function-data.h"
#include "compiler/data/lambda-generator.h"
#include "compiler/gentree.h"
#include "compiler/vertex.h"

LambdaGenerator::LambdaGenerator(const std::string &name, const Location &location)
  : created_location(location)
{
  auto lambda_class_name = create_name(name);
  generated_lambda = create_class(lambda_class_name);
}

LambdaGenerator &LambdaGenerator::add_uses(std::vector<VertexPtr> uses, bool implicit_capture_this /*= false*/) {
  kphp_assert_msg(generated_lambda, "lambda was already generated by this class");

  if (implicit_capture_this) {
    auto implicit_captured_var_parent_this = VertexAdaptor<op_var>::create();
    implicit_captured_var_parent_this->set_string("parent$this");
    set_location(implicit_captured_var_parent_this, created_location);

    auto func_param = VertexAdaptor<op_func_param>::create(implicit_captured_var_parent_this);
    set_location(func_param, created_location);

    uses.insert(uses.begin(), func_param);
  }

  for (auto one_use : uses) {
    if (auto param_as_use = one_use.try_as<op_func_param>()) {
      auto variable_in_use = VertexAdaptor<op_class_var>::create();
      variable_in_use->str_val = param_as_use->var()->get_string();
      set_location(variable_in_use, param_as_use->location);
      generated_lambda->members.add_instance_field(variable_in_use, access_private);
    }
  }

  this->uses = std::move(uses);

  return *this;
}

LambdaGenerator &LambdaGenerator::add_invoke_method(const VertexAdaptor<op_function> &function) {
  auto name = create_name("__invoke");
  auto params = create_invoke_params(function);
  auto cmd = create_invoke_cmd(function);
  auto invoke_function = VertexAdaptor<op_function>::create(name, params, cmd);
  set_location(invoke_function, created_location);

  register_invoke_method(invoke_function);

  return *this;
}

LambdaGenerator &LambdaGenerator::add_constructor_from_uses() {
  auto constructor_params = VertexAdaptor<op_func_param_list>::create(uses);
  auto name_location = generated_lambda->root->name()->location;
  set_location(constructor_params, name_location);
  generated_lambda->create_constructor_with_args(name_location.line, constructor_params);
  generated_lambda->construct_function->is_template = !uses.empty();

  return *this;
}

LambdaGenerator &LambdaGenerator::add_invoke_method_which_call_function(FunctionPtr called_function) {
  auto lambda_params = get_params_as_vector_of_vars(called_function);
  auto call_function = VertexAdaptor<op_func_call>::create(lambda_params);

  call_function->set_string(called_function->name);
  call_function->set_func_id(called_function);
  return create_invoke_fun_returning_call(call_function, called_function->root->params());
}

LambdaPtr LambdaGenerator::generate_and_require(FunctionPtr parent_function, DataStream<FunctionPtr> &os) {
  auto lambda_class = generate(std::move(parent_function));
  lambda_class->members.for_each([&os](const ClassMemberInstanceMethod &m) {
    G->require_function(m.function, os);
  });

  return lambda_class;
}

LambdaPtr LambdaGenerator::generate(FunctionPtr parent_function) {
  kphp_assert(generated_lambda);
  generated_lambda->members.for_each([&parent_function](ClassMemberInstanceMethod &m) {
    m.function->function_in_which_lambda_was_created = parent_function;
  });

  G->register_class(generated_lambda);

  return std::move(generated_lambda);
}

VertexAdaptor<op_func_name> LambdaGenerator::create_name(const std::string &name) {
  auto lambda_class_name = VertexAdaptor<op_func_name>::create();
  lambda_class_name->set_string(name);
  set_location(lambda_class_name, created_location);

  return lambda_class_name;
}

LambdaPtr LambdaGenerator::create_class(VertexAdaptor<op_func_name> name) {
  LambdaPtr anon_class(new LambdaClassData());
  anon_class->set_name_and_src_name(LambdaClassData::get_lambda_namespace() + "\\" + name->get_string());
  anon_class->root = VertexAdaptor<op_class>::create(name);

  return anon_class;
}

VertexPtr LambdaGenerator::create_invoke_cmd(VertexAdaptor<op_function> function) {
  auto new_cmd = function->cmd().clone();
  // if we didn't do it early
  if (!function->get_func_id() || !function->get_func_id()->function_in_which_lambda_was_created) {
    add_this_to_captured_variables(new_cmd);
  }
  return new_cmd;
}

VertexAdaptor<op_func_param_list> LambdaGenerator::create_invoke_params(VertexAdaptor<op_function> function) {
  std::vector<VertexPtr> func_parameters;
  generated_lambda->patch_func_add_this(func_parameters, created_location.line);
  auto params_range = get_function_params(function);
  if (function->get_func_id() && (function->get_func_id()->function_in_which_lambda_was_created || function->get_func_id()->is_lambda())) {
    kphp_assert(params_range.size() > 0);
    // skip $this parameter, which was added to `function` previously
    func_parameters.insert(func_parameters.end(), params_range.begin() + 1, params_range.end());
  } else {
    func_parameters.insert(func_parameters.end(), params_range.begin(), params_range.end());
  }

  // every parameter (excluding $this) could be any class_instance
  for (size_t i = 1, id = 0; i < func_parameters.size(); ++i) {
    auto param = func_parameters[i].as<op_func_param>();
    if (param->type_declaration == "callable") {
      param->template_type_id = static_cast<int>(id);
      param->is_callable = true;
      id++;
    } else if (param->type_declaration.empty()) {
      param->template_type_id = static_cast<int>(id);
      id++;
    }
  }

  auto params = VertexAdaptor<op_func_param_list>::create(func_parameters);
  params->location.line = function->params()->location.line;

  return params;
}

void LambdaGenerator::add_this_to_captured_variables(VertexPtr &root) {
  if (root->type() != op_var) {
    for (auto &v : *root) {
      add_this_to_captured_variables(v);
    }
    return;
  }

  if (generated_lambda->members.get_instance_field(root->get_string())) {
    auto inst_prop = VertexAdaptor<op_instance_prop>::create(ClassData::gen_vertex_this(-1));
    inst_prop->location = root->location;
    inst_prop->str_val = root->get_string();
    root = inst_prop;
  } else if (root->get_string() == "this") {
    // replace `$this` with `$this->parent$this`
    auto new_root = VertexAdaptor<op_instance_prop>::create(root);
    new_root->set_string("parent$this");
    set_location(new_root, root->location);
    root = new_root;
  }
}

FunctionPtr LambdaGenerator::register_invoke_method(VertexAdaptor<op_function> fun) {
  auto fun_name = fun->name()->get_string();
  fun->name()->set_string(replace_backslashes(generated_lambda->name) + "$$" + fun_name);
  auto invoke_function = FunctionData::create_function(fun, FunctionData::func_type_t::func_local);
  invoke_function->update_location_in_body();
  generated_lambda->members.add_instance_method(invoke_function, AccessType::access_public);

  auto params = get_function_params(fun);
  invoke_function->is_template = generated_lambda->members.has_any_instance_var() || params.size() > 1;
  invoke_function->root->inline_flag = true;

  //TODO: need set function_in_which_created for all lambdas inside
  //invoke_function->lambdas_inside = std::move(lambdas_inside);
  //for (auto &l : invoke_function->lambdas_inside) {
  //  l->function_in_which_lambda_was_created = invoke_function;
  //}

  G->register_function(invoke_function);

  return invoke_function;
}

LambdaGenerator &LambdaGenerator::create_invoke_fun_returning_call(VertexAdaptor<op_func_call> &call_function, VertexAdaptor<op_func_param_list> invoke_params) {
  auto return_call = VertexAdaptor<op_return>::create(call_function);
  auto lambda_body = VertexAdaptor<op_seq>::create(return_call);

  set_location(created_location, call_function, return_call, lambda_body);

  auto fun = VertexAdaptor<op_function>::create(generated_lambda->root->name(), invoke_params, lambda_body);
  add_invoke_method(fun);

  return *this;
}

std::vector<VertexAdaptor<op_var>> LambdaGenerator::get_params_as_vector_of_vars(FunctionPtr function, int shift) {
  auto func_params = function->get_params();
  kphp_assert(func_params.size() >= shift);

  std::vector<VertexAdaptor<op_var>> res_params(static_cast<size_t>(func_params.size() - shift));
  std::transform(std::next(func_params.begin(), shift), func_params.end(), res_params.begin(),
                 [](VertexAdaptor<op_func_param> param) {
                   auto new_param = VertexAdaptor<op_var>::create();
                   new_param->set_string(param->var()->get_string());
                   return new_param;
                 }
  );

  return res_params;
}
