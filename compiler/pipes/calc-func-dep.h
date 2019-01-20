#pragma once

#include <vector>

#include "compiler/function-pass.h"

struct DepData {
  std::vector<FunctionPtr> dep;
  std::vector<VarPtr> used_global_vars;

  std::vector<VarPtr> used_ref_vars;
  std::vector<std::pair<VarPtr, VarPtr>> ref_ref_edges;
  std::vector<std::pair<VarPtr, VarPtr>> global_ref_edges;

  std::vector<FunctionPtr> forks;

  // copy of DepData is probably a bug
  // feel free to remove it if not
  DepData(const DepData&) = delete;
  DepData& operator=(const DepData&) = delete;

  DepData() = default;
  ~DepData() = default;
  DepData(DepData&&) = default;
  DepData& operator=(DepData&&) = default;
};

static_assert(std::is_nothrow_move_constructible<DepData>::value, "aaa");

class CalcFuncDepPass : public FunctionPassBase {
private:
  DepData data;
public:
  struct LocalT : public FunctionPassBase::LocalT {
    VertexAdaptor<op_func_call> extern_func_call;
  };

  string get_description() {
    return "Calc function depencencies";
  }

  bool check_function(FunctionPtr function);

  void on_enter_edge(VertexPtr, LocalT *local, VertexPtr, LocalT *dest_local);

  VertexPtr on_enter_vertex(VertexPtr vertex, LocalT *local);

  DepData on_finish();
};
